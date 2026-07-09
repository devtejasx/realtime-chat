# realtime-chat

A production-grade real-time chat backend in modern **C++20**, built milestone
by milestone the way a real engineering team would: small vertical slices,
tests with every feature, explicit design decisions.

**Status: Milestone 1 — project skeleton, build system, configuration, health endpoint.** ✅

## Tech stack

| Concern          | Choice                          |
|------------------|---------------------------------|
| Language         | C++20                           |
| HTTP / WebSocket | [Crow](https://crowcpp.org)     |
| Database         | PostgreSQL (via libpqxx) — M2   |
| Cache / sessions | Redis — M8                      |
| Auth             | JWT + bcrypt — M3/M4            |
| Build            | CMake ≥ 3.24 + Ninja            |
| Tests            | GoogleTest                      |
| Containers       | Docker + Compose — M12          |
| CI/CD            | GitHub Actions — M14            |
| Deploy           | AWS EC2 + RDS — M15             |

## Architecture (target)

Clean layered architecture. Requests flow downward only; no layer knows about
the layer above it.

```
HTTP/WS request
      │
middlewares/     auth, rate limiting, logging          (cross-cutting)
      │
controllers/     parse/validate HTTP, map to DTOs      (no business logic)
      │
services/        business rules, transactions          (no HTTP, no SQL)
      │
repositories/    all SQL, one repo per aggregate       (no business logic)
      │
database/        connection pooling, migrations
```

`websocket/` handles real-time delivery and calls into the same `services/`
layer the REST controllers use — business logic exists exactly once.

## Directory layout

```
realtime-chat/
├── CMakeLists.txt          # targets: chat_lib, chat_server, chat_tests
├── cmake/dependencies.cmake# pinned third-party deps (FetchContent)
├── src/
│   ├── main.cc             # thin entry point
│   ├── application.{h,cc}  # composition root; owns Crow app + lifecycle
│   ├── config/             # env-based configuration (12-factor)
│   ├── controllers/        # (M5)
│   ├── services/           # (M3+)
│   ├── repositories/       # (M2+)
│   ├── models/             # (M2+)
│   ├── middlewares/        # (M4)
│   ├── websocket/          # (M6)
│   ├── database/           # (M2)
│   └── utils/
├── tests/                  # GoogleTest suite
└── docs/                   # API docs per milestone
```

## Building

Requires a C++20 compiler, CMake ≥ 3.24 and Ninja. Dependencies (Crow, Asio,
GoogleTest) are downloaded and pinned automatically at configure time.

### Windows (MSYS2 UCRT64)

```powershell
# One-time toolchain setup
C:\msys64\usr\bin\pacman.exe -S --needed mingw-w64-ucrt-x86_64-gcc `
    mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja

$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Linux / macOS

```bash
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Running

Configuration comes exclusively from environment variables
(see [.env.example](.env.example)). Everything has a sane development default:

```powershell
$env:CHAT_PORT = "8080"
.\build\chat_server.exe
```

Verify:

```
$ curl http://localhost:8080/health
{"version":"0.1.0","status":"ok","service":"realtime-chat","environment":"development"}
```

## Testing

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

## API (so far)

| Method | Path      | Auth | Description                             |
|--------|-----------|------|-----------------------------------------|
| GET    | `/health` | none | Liveness probe: status, version, stage. |

## Roadmap

- [x] **M1** Project setup — skeleton, CMake, config, health endpoint, tests
- [ ] **M2** Database setup — PostgreSQL schema, migrations, connection pool
- [ ] **M3** Authentication — register/login, bcrypt
- [ ] **M4** JWT middleware
- [ ] **M5** REST APIs — profiles, conversations
- [ ] **M6** WebSocket server
- [ ] **M7** Real-time chat
- [ ] **M8** Redis sessions
- [ ] **M9** Typing indicators
- [ ] **M10** Read receipts
- [ ] **M11** File upload
- [ ] **M12** Docker
- [ ] **M13** Testing (integration)
- [ ] **M14** CI/CD
- [ ] **M15** AWS deployment
