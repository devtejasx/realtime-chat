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
