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

## Timestamps

Timestamps are stored as `TIMESTAMPTZ`. Repository reads return them as Unix
epoch seconds (`EXTRACT(EPOCH ...)`) to avoid client/server timezone-formatting
ambiguity, and the application formats them as ISO-8601 UTC for API responses.

## Adding a migration

1. Create `migrations/NNNN_description.sql` with the next number.
2. Write forward-only DDL/DML (use `IF NOT EXISTS` where sensible).
3. Restart the server (or run the migration runner) — it applies automatically.
