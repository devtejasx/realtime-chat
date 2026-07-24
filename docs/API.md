# API

Base URL (local): `http://localhost:8080`

All request and response bodies are JSON (`Content-Type: application/json`).

## Conventions

### Success

Successful responses return the resource directly with an appropriate status
(`200 OK`, `201 Created`).

### Errors

Every error uses a single envelope:

```json
{
  "error": {
    "code": "validation_error",
    "message": "Human-readable summary",
    "details": "field=username"
  }
}
```

`details` is optional. `code` is stable and machine-readable:

| `code`                 | HTTP | Meaning                          |
| ---------------------- | ---- | -------------------------------- |
| `validation_error`     | 400  | invalid / malformed input        |
| `authentication_error` | 401  | missing or invalid credentials   |
| `authorization_error`  | 403  | authenticated but not permitted  |
| `not_found`            | 404  | resource does not exist          |
| `conflict`             | 409  | duplicate unique value           |
| `database_error`       | 500  | persistence failure              |
| `internal_error`       | 500  | unexpected failure               |

### Authentication

Protected endpoints require a **Bearer access token**:

```
Authorization: Bearer <access_token>
```

Tokens are JWTs (HS256) with `iss`, `iat`, `exp`, `sub` (user id), plus custom
`username` and `type` (`access` / `refresh`) claims. Access tokens are
short-lived (default 15 min); refresh tokens last longer (default 14 days).

---

## `GET /health`

Liveness/status. No authentication.

**200 OK**

```json
{
  "status": "ok",
  "service": "realtime-chat",
  "version": "0.1.0",
  "environment": "development"
}
```

```bash
curl http://localhost:8080/health
```

---

## `POST /api/auth/register`

Create an account and receive tokens.

**Request**

```json
{
  "username": "alice",
  "email": "alice@example.com",
  "password": "password123"
}
```

**Field rules**

| Field      | Rules                                                        |
| ---------- | ----------------------------------------------------------- |
| `username` | 3–32 chars; starts with a letter/digit; `[A-Za-z0-9._-]`    |
| `email`    | valid email, ≤ 254 chars (stored lower-cased)               |
| `password` | 8–72 bytes                                                  |

**201 Created**

```json
{
  "user": {
    "id": 1,
    "username": "alice",
    "email": "alice@example.com",
    "created_at": "2026-07-24T12:00:00Z",
    "updated_at": "2026-07-24T12:00:00Z"
  },
  "tokens": {
    "access_token": "eyJhbGciOi...",
    "refresh_token": "eyJhbGciOi...",
    "token_type": "Bearer",
    "access_expires_in": 900,
    "refresh_expires_in": 1209600
  }
}
```

**Errors:** `400` (invalid body/fields), `409` (username or email taken).

```bash
curl -X POST http://localhost:8080/api/auth/register \
  -H 'Content-Type: application/json' \
  -d '{"username":"alice","email":"alice@example.com","password":"password123"}'
```

---

## `POST /api/auth/login`

Exchange credentials for tokens. The `identifier` may be a username or email.

**Request**

```json
{
  "identifier": "alice",
  "password": "password123"
}
```

> `username` or `email` are also accepted in place of `identifier`.

**200 OK** — same shape as register (`user` + `tokens`).

**Errors:** `400` (missing fields), `401` (invalid credentials). The `401`
message does not reveal whether the account exists.

```bash
curl -X POST http://localhost:8080/api/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"identifier":"alice","password":"password123"}'
```

---

## `GET /api/auth/me`

Return the currently authenticated user. **Requires** a valid access token.

**200 OK**

```json
{
  "id": 1,
  "username": "alice",
  "email": "alice@example.com",
  "created_at": "2026-07-24T12:00:00Z",
  "updated_at": "2026-07-24T12:00:00Z"
}
```

**Errors:** `401` (missing/invalid/expired token).

```bash
curl http://localhost:8080/api/auth/me \
  -H "Authorization: Bearer $ACCESS_TOKEN"
```

---

# Phase 2 — Users, Conversations, Messages, Realtime

All Phase 2 endpoints require a Bearer access token.

## Users / Profiles

### `GET /api/users/me`
Returns the caller's own profile (includes `email`, `display_name`, `bio`,
`avatar_url`).

### `PUT /api/users/me`
Partial profile update. Only supplied keys change; send `null` to clear a field.

```json
{ "display_name": "Alice A.", "bio": "Coffee & C++", "avatar_url": "https://example.com/a.png" }
```

**200 OK** — the updated self profile. **Errors:** `400` (invalid field).

### `GET /api/users/{id}`
Another user's **public** profile (no email).

## Conversations

### `POST /api/conversations`
Create a direct or group conversation.

Direct (deduplicated per user pair):
```json
{ "type": "direct", "participant_ids": [2] }
```
Group (creator becomes owner):
```json
{ "type": "group", "name": "Team", "participant_ids": [2, 3] }
```
**201 Created** — the conversation with its `participants`. Direct creation is
idempotent (returns the existing conversation if present).

### `GET /api/conversations`
List the caller's conversations, most-recently-active first. Supports
`?limit=&offset=`. Returns `{ "conversations": [ ... ] }`.

### `GET /api/conversations/{id}`
Get one conversation (participants only, else `404`).

### `DELETE /api/conversations/{id}`
Delete. Direct: any participant. Group: owner only (`403` otherwise).

### Group management
| Method | Path                                        | Who    |
| ------ | ------------------------------------------- | ------ |
| PATCH  | `/api/conversations/{id}/name`              | owner  |
| POST   | `/api/conversations/{id}/members`           | owner  |
| DELETE | `/api/conversations/{id}/members/{userId}`  | owner  |
| POST   | `/api/conversations/{id}/leave`             | member |

`PATCH .../name` body: `{ "name": "New Name" }`.
`POST .../members` body: `{ "user_id": 4 }`.
When the owner leaves, ownership transfers to the earliest-joined member (or the
group is deleted if none remain).

## Messages

### `POST /api/messages`
Send a message. Persists first, then broadcasts `message.created` to all
participants over WebSocket.
```json
{ "conversation_id": 10, "content": "Hello!" }
```
**201 Created** — the stored message.

### `GET /api/messages`
List/search a conversation's messages. Query params:
`conversation_id` (required), `sender_id`, `q` (keyword, full-text),
`limit`, `offset`, `before` / `after` (keyset cursor on message id).
Returns `{ "messages": [ ... ] }`, newest-first.

### `PATCH /api/messages/{id}`
Edit content (author only). Body: `{ "content": "edited" }`. Broadcasts
`message.updated`.

### `DELETE /api/messages/{id}`
Soft-delete (author or group owner). Broadcasts `message.deleted`; content is
redacted in subsequent reads.

---

# WebSocket API

Endpoint: `ws://localhost:8080/ws?token=<access_token>` (the token may also be
supplied via an `Authorization: Bearer` header). An invalid token fails the
upgrade.

All frames are JSON envelopes:
```json
{ "type": "<event>", "data": { ... } }
```

## Client → server

| Type             | `data`                                             | Effect                          |
| ---------------- | -------------------------------------------------- | ------------------------------- |
| `ping`           | —                                                  | server replies `pong`           |
| `message.send`   | `{ conversation_id, content, type? }`              | persist + broadcast a message   |
| `typing.start`   | `{ conversation_id }`                              | broadcast to room (not stored)  |
| `typing.stop`    | `{ conversation_id }`                              | broadcast to room (not stored)  |
| `mark_delivered` | `{ message_id }`                                   | receipt → delivered             |
| `mark_read`      | `{ conversation_id, up_to_message_id }`            | receipts → read; advance marker |

## Server → client

| Type                            | `data`                                             |
| ------------------------------- | -------------------------------------------------- |
| `ready`                         | `{ user_id, username }` (sent on connect)          |
| `pong`                          | `{}`                                               |
| `message.created` / `.updated` / `.deleted` | the message object                     |
| `conversation.created` / `.deleted`         | conversation / `{ conversation_id }`   |
| `conversation.member_added` / `.member_removed` | `{ conversation_id, user_id }`     |
| `typing.start` / `typing.stop`  | `{ conversation_id, user_id, username }`           |
| `presence.update`               | `{ user_id, status: "online"\|"offline" }`         |
| `receipt.update`                | the receipt `{ message_id, user_id, state }`       |
| `read.update`                   | `{ conversation_id, user_id, up_to_message_id }`   |
| `error`                         | `{ code, message }`                                |

## Heartbeat
The server sends a `ping` every `WS_HEARTBEAT_INTERVAL_SECONDS` and closes any
connection idle beyond `WS_HEARTBEAT_TIMEOUT_SECONDS`. Any inbound frame counts
as activity.
