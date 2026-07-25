# Deployment

This guide covers running realtime-chat in production: scaling, Redis, storage,
security, and operations. See [Architecture.md](Architecture.md) for the
internal design and [.env.example](../.env.example) for the full configuration
surface.

## Topology

```
                     ┌────────────┐
   clients  ───────► │  Load      │ ──► app instance 1 ─┐
   (REST + WS)       │  balancer  │ ──► app instance 2 ─┼─► PostgreSQL (primary)
                     └────────────┘ ──► app instance N ─┘        │
                                                          └────► Redis (shared)
```

- **Stateless app instances.** All durable state is in PostgreSQL; all shared
  ephemeral state (cache, rate-limit counters, presence, sessions) is in Redis.
  This lets you run **N instances** behind a load balancer and scale
  horizontally to target **100k+ users / 10k+ concurrent WebSocket connections**.
- **Sticky sessions are not required** for correctness: any instance can serve
  any request. WebSocket fan-out is per-instance for locally-connected sockets;
  cross-instance delivery uses the shared services and (with Redis) shared
  presence.

## Configuration essentials

Set at minimum in production (see `.env.example` for all):

| Variable                | Notes                                             |
| ----------------------- | ------------------------------------------------- |
| `APP_ENV=production`    | enables strict secret validation                  |
| `JWT_SECRET`            | **required**, ≥ 32 bytes, unique per environment  |
| `DB_*`                  | PostgreSQL connection                             |
| `REDIS_ENABLED=true`    | enable the shared cache backend                   |
| `REDIS_URL`             | e.g. `tcp://redis:6379`                           |
| `UPLOAD_DIR` / storage  | shared/object storage in multi-instance setups    |
| `CORS_ALLOWED_ORIGINS`  | restrict to your web origins (not `*`)            |
| `RATE_LIMIT_*`          | tune per endpoint                                 |

## Redis

Redis powers distributed cache, rate limiting, and fleet-wide presence. To use
it, **build with Redis support** and enable it:

```bash
cmake -S . -B build -DRTC_WITH_REDIS=ON      # requires redis-plus-plus installed
cmake --build build
REDIS_ENABLED=true REDIS_URL=tcp://redis:6379 ./build/bin/realtime-chat
```

If the binary is built without `RTC_WITH_REDIS`, or `REDIS_ENABLED=false`, or
Redis is unreachable at startup, the service **falls back to the in-memory
store** (single-instance semantics) and logs the choice. No code changes are
needed to switch backends — see the `ICacheStore` abstraction.

## File storage

Uploads use the pluggable `IFileStorage` abstraction. The default
`LocalFileStorage` writes under `UPLOAD_DIR`. For multi-instance deployments,
either mount shared storage or implement `IFileStorage` for your object store
(S3/Azure/GCS) and wire it in the composition root — no service-layer change.

## Database

- Run migrations automatically at startup (default) or ahead of a rolling
  deploy; the runner is idempotent.
- Size the connection pool (`DB_POOL_SIZE`) for your instance count so total
  connections stay within the server's `max_connections`.
- All hot-path queries use prepared, fixed-shape statements and appropriate
  indexes (see [Database.md](Database.md)).

## Background jobs

Each instance runs a `BackgroundExecutor` (thumbnailing, push delivery) and a
`PeriodicScheduler` (expired-session cleanup, cache purge). `WORKER_THREADS` and
`MAINTENANCE_INTERVAL_SECONDS` tune them. These are per-instance and safe to run
on every node.

## Security checklist (OWASP-aligned)

- `JWT_SECRET` strong and secret; refresh tokens are **rotated** on every
  refresh and stored only as hashes (replay protection).
- Uploads restricted by an explicit **MIME allow-list**, size limits, and
  extension cross-checks; storage keys are traversal-safe.
- Security headers (`X-Content-Type-Options`, `X-Frame-Options`, CSP, ...) and
  configurable **CORS** applied to every response.
- Rate limiting on login, registration, messaging, and uploads.
- Run the server as a **non-root** user (the Docker image already does).
- Terminate TLS at the load balancer / reverse proxy.

## Containers

```bash
docker compose up --build          # Postgres + server
```

For production, add a Redis service and set `REDIS_ENABLED=true`,
`REDIS_URL=tcp://redis:6379`, and a strong `JWT_SECRET` in the server
environment. Scale app instances behind your load balancer.

## Monitoring

- `GET /metrics` exposes Prometheus metrics: request rate/latency, active users,
  WebSocket connections, cache hit ratio, background queue depth, memory, and
  uptime. Point a Prometheus scraper at each instance.
- `GET /health` is the liveness/readiness probe for orchestrators.
- Logs are structured (spdlog); set `LOG_LEVEL` per environment. Cache,
  WebSocket, upload, auth, rate-limit, and notification events are logged.

## Zero-downtime rolling deploys

1. Apply migrations (backward-compatible; the app tolerates additive changes).
2. Roll instances one at a time behind the load balancer.
3. Clients reconnect WebSockets to healthy instances automatically; presence and
   sessions live in shared state, so no user state is lost.
