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

---

## Real-time layer (Phase 2)

Phase 2 adds WebSockets alongside the existing REST surface. The guiding rule is
unchanged: **all business logic lives in services**. REST controllers and
WebSocket handlers are both thin adapters that call the *same* service methods,
so messaging behaviour is defined once.

### Components (`include/rtc/realtime`)

| Component            | Responsibility                                             |
| -------------------- | ---------------------------------------------------------- |
| `SessionManager`     | thread-safe `connection ↔ user` registry (dual-indexed)    |
| `RoomManager`        | `conversation ↔ connections` routing cache for fan-out     |
| `ConnectionManager`  | owns the two registries; **implements `IEventBroadcaster`**|
| `EventDispatcher`    | decodes `{type,data}` frames → service calls; lifecycle    |
| `HeartbeatMonitor`   | background ping / idle-timeout sweep                       |
| `PresenceService`    | reference-counted online/offline + last-seen (in `services`) |

### The broadcaster seam

Services never depend on the WebSocket layer. They depend on the
`realtime::IEventBroadcaster` interface and call `publish(user_ids, type, data)`
after persisting. `ConnectionManager` implements that interface; a
`NullEventBroadcaster` is used where realtime is disabled (unit tests). This is
what lets a REST `POST /api/messages` and a WebSocket `message.send` produce
identical broadcasts — they run the same `MessageService::send`.

### Persist-first, broadcast-second

Every state-changing realtime action follows the same order inside the service:

1. validate,
2. **persist** (durability before delivery),
3. **broadcast** to participants via the injected broadcaster.

A crash between 2 and 3 loses only a notification, never data; clients reconcile
via the REST list endpoints.

### Connection lifecycle

```
handshake ──onaccept──► verify JWT (query token / Authorization) ─► accept/reject
   │
 onopen ──► register session ─► join conversation rooms ─► presence: online ─► "ready"
   │
onmessage ──► touch activity ─► dispatch (ping/typing/send/mark_*) ─► service ─► broadcast
   │
 onclose ──► unregister session ─► leave all rooms ─► presence: offline (if last session)
```

### Concurrency & performance

- All realtime registries use fine-grained mutexes and O(1) hash lookups,
  sized for thousands of concurrent connections.
- Presence is reference-counted so multi-device users report "offline" only when
  their last session closes.
- Typing indicators route through `RoomManager` (no DB) and are never persisted.
- Message/receipt writes reuse the pooled connections and prepared,
  fixed-shape SQL from the repository layer.
- Broadcast payloads are serialised **once** and delivered to every target
  session.

---

## Production & scalability layer (Phase 3)

Phase 3 makes the service horizontally scalable and production-ready while
preserving the layering: new capabilities are added as **interfaces with default
implementations**, injected in the composition root, so business logic never
changes when a backend is swapped.

### New abstractions (and their default vs. pluggable impls)

| Seam                    | Default (no deps)        | Pluggable alternative        |
| ----------------------- | ------------------------ | ---------------------------- |
| `cache::ICacheStore`    | `InMemoryCacheStore`     | `RedisCacheStore` (`-DRTC_WITH_REDIS`) |
| `storage::IFileStorage` | `LocalFileStorage`       | S3 / Azure / GCS             |
| `media::IImageProcessor`| `NoopImageProcessor`     | stb_image / libvips          |
| `notifications::IPushProvider` | `NullPushProvider`| FCM / APNs / email / SMS     |
| `notifications::INotificationDispatcher` | `NullNotificationDispatcher` | `NotificationDispatcher` |

### Cross-cutting infrastructure

- **CacheService** — namespaced, JSON, read-through, with hit/miss metrics.
- **PresenceCache** — fleet-wide online tracking over the shared store.
- **RateLimiter** — fixed-window limits over `ICacheStore` (per user/IP).
- **BackgroundExecutor** / **PeriodicScheduler** — off-request work (thumbnails,
  push, cache purge, expired-session cleanup).
- **MetricsRegistry** — Prometheus exposition at `GET /metrics`.
- **SecurityMiddleware** / **MetricsMiddleware** — global headers/CORS and
  request timing, added to the Crow middleware stack.

### Event-driven notifications

Domain services (message, conversation, reaction) announce events through the
`INotificationDispatcher` seam **after** they persist and broadcast. The
concrete dispatcher turns events into persisted, delivered notifications via
`NotificationService`, which fans out in-app over WebSocket and dispatches
external push on the background executor. Producers stay decoupled from delivery
policy, and every handler is `noexcept` so notifications never disrupt the
originating operation.

### Distributed sessions

`SessionService` records each login as a session (refresh token stored only as a
SHA-256 hash), supports rotation on refresh (old token rejected — replay
protection), and revocation of one or all sessions. The `sessions` table is the
source of truth; Redis can cache active-session lookups across instances.

### Scaling model

App instances are stateless: durable state in PostgreSQL, shared ephemeral state
in Redis. This supports multi-instance deployment behind a load balancer,
targeting 100k+ users and 10k+ concurrent WebSocket connections. See
[Deployment.md](Deployment.md).
