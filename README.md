# realtime-chat

A production-grade real-time chat backend written in **Modern C++20**.

> **Phase 1 — Foundation & Authentication.** This milestone delivers the
> project foundation: clean architecture, configuration, structured logging,
> a full error-handling stack, PostgreSQL persistence with a connection pool and
> migration runner, user registration/login with bcrypt + JWT, an authentication
> guard for protected routes, a health endpoint, a GoogleTest suite, Docker
> tooling, and documentation. Real-time messaging (WebSockets) arrives in a
> later phase.

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
- **Tests** — GoogleTest covering config, validation, hashing, JWT, services,
  and the register/login APIs, plus opt-in database integration tests.

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

## Further reading

- [docs/Architecture.md](docs/Architecture.md) — layers, dependency injection,
  request lifecycle.
- [docs/Database.md](docs/Database.md) — schema, migrations, connection pool.
- [docs/API.md](docs/API.md) — endpoints, payloads, error format.

## License

Released under the MIT License. See [LICENSE](LICENSE).
