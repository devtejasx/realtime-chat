# realtime-chat

A production-grade real-time chat backend written in **Modern C++20**.

> **Phase 1 — Foundation & Authentication** (complete): clean architecture,
> configuration, structured logging, error handling, PostgreSQL with a
> connection pool and migration runner, bcrypt + JWT auth, an authentication
> guard, a health endpoint, tests, Docker, and docs.
>
> **Phase 2 — Core Real-Time Messaging Platform** (complete): user profiles,
> one-to-one and group conversations, messages with edit/soft-delete, read
> receipts, pagination and full-text search — over both **REST and WebSockets**.
> The WebSocket server adds session/connection/room managers, an event
> dispatcher, presence, typing indicators, heartbeat, and disconnect cleanup.
> REST controllers and WebSocket handlers call the **same service classes**, so
> business logic is never duplicated.
>
> **Phase 3 — Scalability & Production Readiness** (complete): a pluggable cache
> (`ICacheStore`: in-memory default, Redis for multi-instance), distributed
> sessions with refresh-token rotation and multi-device logout, file uploads /
> attachments over a pluggable storage backend (local default; S3/Azure/GCS
> seam), message reactions, an **event-driven notification system** (in-app +
> pluggable FCM/APNs/email/SMS push), a background-job executor and scheduler,
> Redis-backed rate limiting, Prometheus metrics at `/metrics`, and security
> hardening (headers, CORS, MIME allow-list, upload limits). Every new backend
> is an interface with a working default, so the project **builds and runs with
> no external services** and scales out by swapping implementations — no
> business-logic changes. See [docs/Deployment.md](docs/Deployment.md).
>
> **Phase 4 — Production Deployment & DevOps** (complete): production Docker
> image (Redis-enabled, tini, healthcheck) + dev/prod Compose stacks, an
> **Nginx reverse proxy** with TLS termination, HTTP→HTTPS, WebSocket proxying,
> gzip and security headers, **Let's Encrypt** automation, a hardened
> **systemd** unit, **GitHub Actions** CI/CD (build+test+coverage, clang-format/
> clang-tidy, CodeQL, secret scanning, image build/push to GHCR, tagged
> releases), **AWS** deployment scripts (EC2/RDS/ElastiCache reference),
> database backup/restore/migrate/maintenance scripts, **liveness/readiness**
> probes, **structured JSON logs with request ids**, expanded Prometheus
> metrics (DB/cache latency, memory), and a full production docs set. Clone and
> follow [docs/Deployment.md](docs/Deployment.md) to ship it.

## Features

- **Clean architecture** — controllers → services → repositories → database,
  with DTOs, models, middlewares, and a single composition root.
- **Configuration** from environment variables with development defaults and
  fail-fast validation.
- **Structured logging** via spdlog (requests, responses, errors, lifecycle).
- **Error handling** — a typed exception hierarchy mapped to consistent JSON
  error responses.
- **PostgreSQL** persistence (libpqxx) with a thread-safe connection pool,
  a repository base class, and an idempotent migration runner.
- **Authentication** — `POST /api/auth/register` and `POST /api/auth/login`,
  bcrypt password hashing, and JWT access/refresh tokens with expiry.
- **JWT middleware** guarding protected routes (`GET /api/auth/me`), returning
  `401` for missing/invalid tokens.
- **Health endpoint** — `GET /health`.
- **User profiles** — display name, bio, avatar (`GET/PUT /api/users/me`,
  `GET /api/users/{id}`).
- **Conversations** — one-to-one (deduplicated) and group chats with ownership,
  membership, rename/add/remove/leave, and delete.
- **Messages** — send, edit, soft-delete, list with keyset pagination, and
  full-text keyword search.
- **WebSockets** — real-time message/typing/presence/receipt events with
  JWT-authenticated handshakes, heartbeat, and disconnect cleanup.
- **Presence & read receipts** — online/offline/last-seen tracking and
  sent/delivered/read receipt state.
- **Tests** — GoogleTest covering config, validation, hashing, JWT, all
  services, realtime managers, and the register/login APIs, plus opt-in database
  integration tests.

## Tech stack

| Concern        | Choice                          |
| -------------- | ------------------------------- |
| Language       | C++20                           |
| HTTP framework | [Crow](https://github.com/CrowCpp/Crow) |
| Database       | PostgreSQL + [libpqxx](https://github.com/jtv/libpqxx) |
| Auth           | bcrypt + JWT ([jwt-cpp](https://github.com/Thalhammer/jwt-cpp)) |
| Logging        | [spdlog](https://github.com/gabime/spdlog) |
| JSON           | [nlohmann/json](https://github.com/nlohmann/json) |
| Build          | CMake ≥ 3.24 (Ninja recommended) |
| Testing        | GoogleTest                      |
| Container      | Docker + Docker Compose         |

## Repository layout

```
.
├── CMakeLists.txt          # top-level build
├── cmake/                  # dependency resolution
├── include/rtc/            # public headers (by layer)
├── src/                    # implementation (mirrors include/)
├── tests/                  # GoogleTest suite + fakes
├── migrations/             # SQL schema migrations
├── docker/                 # Dockerfile
├── docker-compose.yml      # local postgres + server
├── scripts/                # build / test / run / format helpers
└── docs/                   # Architecture, Database, API
```

## Quick start (Docker)

The fastest path — builds the server and starts PostgreSQL alongside it:

```bash
docker compose up --build
```

Then:

```bash
curl http://localhost:8080/health
```

## Production deployment

The full production stack (PostgreSQL + Redis + app + Nginx/TLS) is one command:

```bash
export JWT_SECRET="$(openssl rand -hex 32)"
export DB_PASSWORD="$(openssl rand -hex 16)"
docker compose -f docker-compose.prod.yml up -d --build
```

Obtain TLS certificates with `DOMAIN=… EMAIL=… ./scripts/ssl/init-letsencrypt.sh`.
For EC2/RDS/ElastiCache, systemd, backups, and the operational runbook, see
[docs/Deployment.md](docs/Deployment.md), [docs/Docker.md](docs/Docker.md), and
[docs/AWS.md](docs/AWS.md). Probes: `/health/live`, `/health/ready`; metrics:
`/metrics`.

## Build from source

**Prerequisites:** a C++20 compiler, CMake ≥ 3.24, Ninja (optional), plus the
PostgreSQL and OpenSSL development headers. Remaining dependencies (Crow,
libpqxx, spdlog, jwt-cpp, bcrypt, GoogleTest) are fetched automatically by
CMake at configure time, so a network connection is required for the first
configure.

On Debian/Ubuntu:

```bash
sudo apt-get install -y build-essential cmake ninja-build git \
    libssl-dev libpq-dev postgresql-server-dev-all
```

Configure and build:

```bash
./scripts/build.sh          # or: cmake -S . -B build && cmake --build build
```

Run the tests:

```bash
./scripts/test.sh
```

Run the server (needs a reachable PostgreSQL — see `docker-compose.yml`):

```bash
cp .env.example .env        # adjust as needed
./scripts/run.sh
```

## Configuration

All settings come from environment variables and have development defaults.
See [`.env.example`](.env.example) for the full list (`CHAT_PORT`, `DB_*`,
`JWT_*`, `LOG_LEVEL`, `APP_ENV`).

## API

See [docs/API.md](docs/API.md) for full endpoint documentation. In brief:

| Method | Path                 | Auth | Description                 |
| ------ | -------------------- | ---- | --------------------------- |
| GET    | `/health`            | —    | Liveness/status             |
| POST   | `/api/auth/register` | —    | Create an account           |
| POST   | `/api/auth/login`    | —    | Obtain access/refresh tokens|
| GET    | `/api/auth/me`       | JWT  | Current authenticated user  |
| GET/PUT| `/api/users/me`      | JWT  | View / update your profile  |
| GET    | `/api/users/{id}`    | JWT  | Another user's public profile |
| POST/GET | `/api/conversations` | JWT | Create / list conversations |
| GET/DELETE | `/api/conversations/{id}` | JWT | Get / delete a conversation |
| POST/GET | `/api/messages`    | JWT  | Send / list & search messages |
| PATCH/DELETE | `/api/messages/{id}` | JWT | Edit / soft-delete a message |
| WS     | `/ws?token=…`        | JWT  | Real-time events (see [API.md](docs/API.md)) |
| POST/GET/DELETE | `/api/attachments…` | JWT | Upload / download / delete files |
| POST/GET/DELETE | `/api/messages/{id}/reactions` | JWT | Reactions |
| GET/POST/DELETE | `/api/notifications…` | JWT | Notification feed |
| GET/DELETE | `/api/sessions…`  | JWT  | Active sessions / revoke |
| POST   | `/api/auth/refresh\|logout\|logout-all` | JWT | Session lifecycle |
| GET    | `/metrics`           | —    | Prometheus metrics |

Group management (`/api/conversations/{id}/name|members|leave`), the full
WebSocket event set, and all Phase 3 endpoints are documented in
[docs/API.md](docs/API.md).

## Further reading

- [docs/Architecture.md](docs/Architecture.md) — layers, dependency injection,
  request lifecycle.
- [docs/Architecture.md](docs/Architecture.md) — layers, DI, request lifecycle,
  real-time and production layers.
- [docs/Database.md](docs/Database.md) — schema, migrations, connection pool.
- [docs/API.md](docs/API.md) — endpoints, payloads, error format.
- [docs/Deployment.md](docs/Deployment.md) — scaling, Redis, storage, rolling
  deploys, readiness checklist, runbook.
- [docs/Docker.md](docs/Docker.md) — images and Compose stacks.
- [docs/AWS.md](docs/AWS.md) — EC2/RDS/ElastiCache reference deployment.
- [docs/Security.md](docs/Security.md) — OWASP posture.
- [docs/Monitoring.md](docs/Monitoring.md) — metrics, probes, alerts.
- [docs/Performance.md](docs/Performance.md) — tuning and scaling.
- [docs/Troubleshooting.md](docs/Troubleshooting.md) — common issues.
- [docs/Contributing.md](docs/Contributing.md) — dev workflow and standards.
- [docs/Release.md](docs/Release.md) — versioning and release process.

## License

Released under the MIT License. See [LICENSE](LICENSE).
