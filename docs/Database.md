# Database

realtime-chat uses **PostgreSQL** accessed through **libpqxx**. This document
covers the schema, migrations, and the connection pool.

## Connection

Connection settings come from configuration (`DB_HOST`, `DB_PORT`, `DB_NAME`,
`DB_USER`, `DB_PASSWORD`). `Config::database_connection_string()` builds a
libpq key/value string; a redacted variant is used for logging so the password
is never written to logs.

## Connection pool

`rtc::database::ConnectionPool` is a fixed-size, thread-safe pool
(`DB_POOL_SIZE`, default 8):

- Connections are opened **eagerly** at construction.
- `acquire()` returns a `PooledConnection` (an RAII lease) that returns the
  connection to the pool on destruction. It **blocks** until one is free.
- Broken connections are detected and transparently **replaced** on lease, so
  callers always receive an open connection.
- Synchronised with a mutex + condition variable.

Repositories never touch the pool directly; `BaseRepository::with_transaction`
acquires a lease, opens a `pqxx::work`, runs the callable, and commits (rolling
back on exception).

## Migrations

SQL migrations live in `migrations/` and are named with a sortable numeric
prefix, e.g. `0001_create_users.sql`.

`rtc::database::MigrationRunner`:

1. ensures a bookkeeping table `schema_migrations (version, applied_at)` exists,
2. lists `*.sql` files in filename order,
3. skips versions already recorded,
4. applies each pending migration and records it **atomically** inside one
   transaction.

Migrations run automatically at server startup (`Application::bootstrap`). The
runner is idempotent: re-running applies nothing new.

## Schema

### `users`

| Column          | Type           | Constraints                          |
| --------------- | -------------- | ------------------------------------ |
| `id`            | `BIGSERIAL`    | primary key                          |
| `username`      | `VARCHAR(32)`  | not null, unique (case-insensitive)  |
| `email`         | `VARCHAR(254)` | not null, unique (case-insensitive)  |
| `password_hash` | `TEXT`         | not null (bcrypt)                    |
| `created_at`    | `TIMESTAMPTZ`  | not null, default `now()`            |
| `updated_at`    | `TIMESTAMPTZ`  | not null, default `now()`, trigger-maintained |

**Indexes / constraints**

- `PRIMARY KEY (id)`
- `UNIQUE INDEX ux_users_username_lower ON (LOWER(username))`
- `UNIQUE INDEX ux_users_email_lower ON (LOWER(email))`

Case-insensitive unique indexes prevent `Alice` and `alice` from registering as
distinct accounts; the application also lower-cases emails before insertion.

**Triggers**

- `trg_users_set_updated_at` — `BEFORE UPDATE`, sets `updated_at = now()` via
  the `set_updated_at()` trigger function, making the database the single source
  of truth for modification time.

### `users` (Phase 2 additions)

Migration `0002` adds nullable profile columns: `display_name VARCHAR(64)`,
`bio VARCHAR(500)`, `avatar_url VARCHAR(2048)`.

### `conversations`

| Column            | Type          | Notes                                        |
| ----------------- | ------------- | -------------------------------------------- |
| `id`              | `BIGSERIAL`   | primary key                                  |
| `type`            | `VARCHAR(16)` | `direct` \| `group` (CHECK)                  |
| `name`            | `VARCHAR(128)`| group name (NULL for direct)                 |
| `owner_id`        | `BIGINT`      | FK → users(id) ON DELETE SET NULL            |
| `direct_key`      | `VARCHAR(64)` | canonical `minId:maxId` for direct chats     |
| `created_at`      | `TIMESTAMPTZ` | default now()                                |
| `updated_at`      | `TIMESTAMPTZ` | trigger-maintained                           |
| `last_message_at` | `TIMESTAMPTZ` | advanced on each new message; sort key       |

- **Unique** partial index `ux_conversations_direct_key ON (direct_key) WHERE
  direct_key IS NOT NULL` — the DB-level guarantee against duplicate one-to-one
  conversations.
- Index `ix_conversations_last_message_at ON (last_message_at DESC NULLS LAST)`
  for "list my conversations, most recent first".

### `conversation_participants`

| Column                 | Type          | Notes                                  |
| ---------------------- | ------------- | -------------------------------------- |
| `id`                   | `BIGSERIAL`   | primary key                            |
| `conversation_id`      | `BIGINT`      | FK → conversations(id) ON DELETE CASCADE |
| `user_id`              | `BIGINT`      | FK → users(id) ON DELETE CASCADE       |
| `role`                 | `VARCHAR(16)` | `owner` \| `member` (CHECK)            |
| `joined_at`            | `TIMESTAMPTZ` | default now()                          |
| `last_read_message_id` | `BIGINT`      | read high-water mark (monotonic)       |

- **Unique** index `ux_participants_conversation_user ON (conversation_id,
  user_id)` — a user appears once per conversation.
- Index `ix_participants_user ON (user_id)` for membership and "list mine".

### `messages`

| Column            | Type          | Notes                                    |
| ----------------- | ------------- | ---------------------------------------- |
| `id`              | `BIGSERIAL`   | primary key                              |
| `conversation_id` | `BIGINT`      | FK → conversations(id) ON DELETE CASCADE |
| `sender_id`       | `BIGINT`      | FK → users(id) ON DELETE CASCADE         |
| `type`            | `VARCHAR(16)` | `text` \| `system` (CHECK)               |
| `content`         | `TEXT`        | redacted in responses when deleted       |
| `created_at`      | `TIMESTAMPTZ` | default now()                            |
| `updated_at`      | `TIMESTAMPTZ` | trigger-maintained                       |
| `edited_at`       | `TIMESTAMPTZ` | set on edit                              |
| `deleted_at`      | `TIMESTAMPTZ` | soft delete                              |

- Index `ix_messages_conversation_id ON (conversation_id, id DESC)` — serves the
  primary access pattern (a conversation's messages, newest-first, keyset).
- Index `ix_messages_sender ON (sender_id)` for sender filtering.
- GIN index `ix_messages_content_fts ON to_tsvector('simple', content)` for
  full-text keyword search.

### `read_receipts`

| Column       | Type          | Notes                                     |
| ------------ | ------------- | ----------------------------------------- |
| `id`         | `BIGSERIAL`   | primary key                               |
| `message_id` | `BIGINT`      | FK → messages(id) ON DELETE CASCADE       |
| `user_id`    | `BIGINT`      | FK → users(id) ON DELETE CASCADE          |
| `state`      | `VARCHAR(16)` | `sent` \| `delivered` \| `read` (CHECK)   |
| `updated_at` | `TIMESTAMPTZ` | trigger-maintained                        |

- **Unique** index `ux_read_receipts_message_user ON (message_id, user_id)` —
  one receipt per (message, user); makes upserts idempotent. State only ever
  advances (`sent → delivered → read`).

## Query design notes

- **Duplicate direct chats** are prevented with an `INSERT ... ON CONFLICT
  (direct_key) ... DO UPDATE` upsert — race-safe and always returns one row.
- **Message creation** inserts the row and advances the conversation's
  `last_message_at` in the *same transaction* (persist + side effect atomic).
- **Listing/search** is a single fixed-shape statement with optional predicates
  (`$n IS NULL OR ...`) so PostgreSQL prepares it once; keyword search hits the
  GIN index and skips deleted rows.
- **Read receipts** advance via `ON CONFLICT ... DO UPDATE` guarded by a rank
  comparison, so a late "delivered" never overwrites a "read".

## Timestamps

Timestamps are stored as `TIMESTAMPTZ`. Repository reads return them as Unix
epoch seconds (`EXTRACT(EPOCH ...)`) to avoid client/server timezone-formatting
ambiguity, and the application formats them as ISO-8601 UTC for API responses.

## Adding a migration

1. Create `migrations/NNNN_description.sql` with the next number.
2. Write forward-only DDL/DML (use `IF NOT EXISTS` where sensible).
3. Restart the server (or run the migration runner) — it applies automatically.
