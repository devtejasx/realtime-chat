# Architecture

realtime-chat follows a layered **clean architecture**. Dependencies point
inward: outer layers (HTTP) depend on inner layers (domain), never the reverse.
Concrete implementations are bound to interfaces in a single composition root,
so every component is independently testable.

## Layers

```
            ┌─────────────────────────────────────────────┐
  HTTP      │  Controllers  ·  Middlewares  ·  DTOs        │
            └───────────────┬─────────────────────────────┘
                            │ depends on
            ┌───────────────▼─────────────────────────────┐
  Domain    │  Services  (business logic)                  │
            └───────────────┬─────────────────────────────┘
                            │ depends on (interfaces)
            ┌───────────────▼─────────────────────────────┐
  Data      │  Repositories  ·  Models                     │
            └───────────────┬─────────────────────────────┘
                            │ depends on
            ┌───────────────▼─────────────────────────────┐
  Infra     │  Database (pool, migrations) · Security      │
            │  Config · Logging · Errors · Utils           │
            └─────────────────────────────────────────────┘
```

### Responsibilities

| Layer            | Contains                              | Must **not** contain            |
| ---------------- | ------------------------------------- | ------------------------------- |
| **Controllers**  | HTTP parsing, status codes, headers   | business rules, SQL             |
| **Middlewares**  | cross-cutting HTTP concerns (auth, logging) | business rules            |
| **DTOs**         | request/response shapes, (de)serialisation | persistence, logic         |
| **Services**     | business logic, orchestration         | HTTP, SQL                       |
| **Repositories** | database operations, row mapping      | business rules, HTTP            |
| **Models**       | domain entities (plain data)          | behaviour                       |
| **Infra**        | config, logging, errors, pool, hashing, JWT | domain logic              |

The rule enforced throughout: **business logic lives only in services**;
**repositories contain only database operations**; **controllers contain only
HTTP handling**.

## Directory ↔ layer mapping

| Path                          | Layer / role                        |
| ----------------------------- | ----------------------------------- |
| `include/rtc/config`          | configuration                       |
| `include/rtc/logging`         | structured logging (spdlog facade)  |
| `include/rtc/errors`          | exception hierarchy + HTTP mapper   |
| `include/rtc/utils`           | env, time helpers                   |
| `include/rtc/security`        | password hashing, JWT tokens        |
| `include/rtc/database`        | connection pool, base repo, migrations |
| `include/rtc/models`          | domain entities                     |
| `include/rtc/dto`             | data-transfer objects               |
| `include/rtc/validation`      | input validation                    |
| `include/rtc/repositories`    | persistence interfaces + PG impls   |
| `include/rtc/services`        | business logic                      |
| `include/rtc/middlewares`     | auth guard, request logging         |
| `include/rtc/controllers`     | HTTP endpoints                      |
| `include/rtc/http`            | Crow app type, response helpers     |
| `include/rtc/application.hpp` | composition root                    |

## Dependency injection

`rtc::Application` is the **composition root**. It is the only place that:

1. constructs concrete implementations (`BcryptPasswordHasher`,
   `JwtTokenService`, `PgUserRepository`), and
2. injects them, via interfaces, into the components that need them.

```
Config ─► Application
             ├─ ConnectionPool
             ├─ IPasswordHasher   ← BcryptPasswordHasher
             ├─ ITokenService     ← JwtTokenService
             ├─ IUserRepository   ← PgUserRepository(pool)
             ├─ UserService(repo, hasher)
             ├─ AuthService(userService, tokenService)
             ├─ AuthMiddleware(tokenService)
             ├─ HealthController(config)
             └─ AuthController(authService, userService, authGuard)
```

Because services depend on **interfaces** (`IUserRepository`,
`IPasswordHasher`, `ITokenService`), unit tests substitute in-memory fakes with
no database or crypto cost.

## Request lifecycle

For `POST /api/auth/register`:

1. **LoggingMiddleware** logs the inbound request and starts a timer.
2. **AuthController** parses the JSON body into a `RegisterRequest` DTO
   (structural validation → `400` on malformed input).
3. **AuthService** validates + normalises the DTO, then delegates to
   **UserService**.
4. **UserService** hashes the password (`IPasswordHasher`) and calls
   **PgUserRepository** to insert the row.
5. **PgUserRepository** runs the SQL inside a transaction (via `BaseRepository`),
   translating a unique-constraint violation into a `ConflictException` (`409`).
6. **AuthService** mints an access/refresh token pair (`ITokenService`) and
   builds an `AuthResponse`.
7. The controller serialises the response (`201`).
8. **LoggingMiddleware** logs the response and elapsed time.

Any exception thrown along the way is caught by `run_guarded`, which maps it to
a consistent JSON error via `ErrorMapper` (see below).

## Error handling

- A typed hierarchy rooted at `AppException` carries an `ErrorType`, an HTTP
  status, a stable machine `code`, a message, and optional client-safe details.
- `run_guarded` wraps every handler body: `AppException` → its declared status
  (logged at warn); any other `std::exception` → a masked `500` (logged at
  error, message never leaked).
- `ErrorMapper` renders the canonical envelope:

  ```json
  { "error": { "code": "validation_error", "message": "...", "details": "..." } }
  ```

## Concurrency model

Crow serves requests on a thread pool. Shared state is limited to the
`ConnectionPool`, which is fully synchronised (mutex + condition variable);
leases are handed out per request and returned via RAII. Services and
repositories are stateless beyond their injected collaborators, so they are safe
to share across threads.

## Design principles applied

- **SOLID** — interfaces at every seam; single-responsibility layers.
- **DRY** — one error envelope, one transaction helper, one response helper.
- **RAII** — connection leases, env guards, logger lifetime.
- **Fail fast** — invalid configuration aborts startup with a clear message.
