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

## Running more than one instance

Two settings control this, and both must be on:

| Variable | Effect |
| --- | --- |
| `REDIS_ENABLED=true` | Shared cache and rate-limit state |
| `CLUSTER_ENABLED=true` | Redis Pub/Sub fan-out between instances (`RedisClusterBus` instead of the no-op bus) |

`CLUSTER_ENABLED` requires `REDIS_ENABLED` — Redis Pub/Sub is the transport — and
config validation rejects the combination at startup rather than degrading
quietly.

**Read this before scaling past one replica.** If the cluster bus is not
available, the service does not fail. It runs, stays healthy, and delivers
WebSocket events only to clients connected to *that* instance. A message sent
through pod A never reaches a client on pod B. Nothing errors and no probe goes
red; chat just works for some recipients and not others, depending on where the
load balancer put them.

Check which mode you are in:

```bash
curl -s localhost:8080/health/ready | jq .cluster
```

```json
{ "distributed": true, "node_id": "node-a" }
```

`"distributed": false` with more than one replica running means delivery is
partial right now.

### Verifying multi-instance delivery locally

`docker-compose.cluster.yml` runs PostgreSQL, Redis and two servers behind an
Nginx balancer. Each server is also published directly, which is the point —
addressing them individually is what turns "it seemed to work" into proof.

```bash
docker compose -f docker-compose.cluster.yml up --build
```

| Endpoint | What it is |
| --- | --- |
| `localhost:8080` | Nginx, round-robin across both |
| `localhost:8081` | Server A directly |
| `localhost:8082` | Server B directly |

**1. Confirm both instances are clustered.** If either reports `false`, stop
here — nothing below will mean anything.

```bash
curl -s localhost:8081/health/ready | jq -c .cluster; curl -s localhost:8082/health/ready | jq -c .cluster
```

**2. Register a user and connect a WebSocket to server A**, using any client
that can hold a connection open (`websocat` below):

```bash
websocat "ws://localhost:8081/api/v1/ws?token=$ACCESS_TOKEN&protocol=2"
```

**3. Send a message through server B**, the instance that client is *not*
connected to:

```bash
curl -sX POST localhost:8082/api/v1/messages -H "Authorization: Bearer $OTHER_TOKEN" -H 'Content-Type: application/json' -d '{"conversation_id":1,"content":"crossed instances"}'
```

The frame arriving on the socket attached to A is the whole proof: it was
persisted by B, published to Redis, received by A, and delivered to A's
connection. Before Redis Pub/Sub, that message would have been written to the
database and silently never delivered.

**4. Watch the hop happen.** Each instance logs the events it publishes and
receives, and the counters are on `/metrics`:

```bash
curl -s localhost:8081/metrics | grep rtc_cluster
```

`rtc_cluster_published_total` climbing on B while `rtc_cluster_received_total`
climbs on A is the fan-out working. Published rising with received flat
everywhere means the instances are talking to different Redis databases, or to
none.

**5. Subscribe to Redis directly** to see the raw traffic:

```bash
docker exec -it rtc-cluster-redis redis-cli psubscribe 'rtc:cluster:*'
```

### What crosses instances, and how

| Concern | Channel | Notes |
| --- | --- | --- |
| Messages, typing, read receipts | `rtc:cluster:user`, `rtc:cluster:room` | Persist first, then broadcast |
| Notifications | `rtc:cluster:user` | Delivered through the same user fan-out; no dedicated channel |
| Presence | `rtc:cluster:presence` | Per-node session sets, so a user on two replicas is online once |
| Cache and authorization invalidation | `rtc:cluster:cache` | Makes a ban effective fleet-wide immediately |
| Sessions, refresh tokens, logout | *(none)* | PostgreSQL-backed, shared by construction |

Loop suppression is by node id: every message carries the publisher's
`RTC_NODE_ID` and receivers drop their own. That is what keeps one send from
becoming two deliveries on the instance that sent it.

### Failure behaviour

| Failure | Effect |
| --- | --- |
| Redis unreachable at startup | Falls back to the no-op bus, logs an error, serves single-instance |
| Redis dies while running | In-flight fan-out is lost; each instance keeps serving its own connections. PostgreSQL is still the source of truth, so a reconnecting client re-reads history and loses nothing durable |
| One instance dies | Its clients reconnect elsewhere. Presence entries it published linger until a peer calls `forget_node` or the users reconnect and re-announce |

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

---

# Production readiness checklist

- [x] **Graceful shutdown** — `SIGTERM` stops accepting connections; the
  heartbeat monitor, scheduler, and background executor drain and join; RAII
  unwinds the pool. tini (container) / systemd forward the signal correctly.
- [x] **Zero-downtime deploys** — stateless instances behind a load balancer;
  migrate once (`server --migrate`), then roll instances one at a time. Nginx
  and ≥2 instances keep traffic served throughout.
- [x] **Migration strategy** — forward-only, idempotent, tracked in
  `schema_migrations`; applied ahead of the roll so old and new instances run
  against a compatible schema.
- [x] **Rollback strategy** — `deploy/aws/rollback.sh` reverts to the previous
  revision; for schema regressions, restore the pre-migration backup
  (`scripts/db/rollback.sh`).
- [x] **Disaster recovery** — nightly `pg_dump` backups with checksums and
  retention (`scripts/db/backup.sh`); RDS Multi-AZ + automated snapshots in AWS.
- [x] **Backup verification** — restores are checksum-verified; periodically
  restore into a scratch database and run `/health/ready` against it.
- [x] **Monitoring** — `/metrics` (Prometheus), `/health/*` probes, alerts (see
  [Monitoring.md](Monitoring.md)).
- [x] **Logging** — structured JSON logs with request ids; JSON Nginx access
  logs; ship to a central aggregator.
- [x] **Scaling** — horizontal via shared Postgres + Redis; read replicas /
  PgBouncer for the database as load grows.
- [x] **Security** — TLS 1.2+, HSTS, security headers, CORS, rate limiting,
  refresh-token rotation, upload hardening (see [Security.md](Security.md)).

## Operational runbook

**Deploy a new version**
1. Merge to `main` (CI green). Tag `vX.Y.Z` to build the image + release.
2. `scripts/db/backup.sh` — take a pre-deploy backup.
3. `docker compose -f docker-compose.prod.yml run --rm server --migrate`.
4. Roll instances (`deploy/aws/deploy.sh vX.Y.Z`), verify `health-check.sh`.

**Incident: high 5xx / latency**
1. Check `/health/ready` and `/metrics` (`rtc_db_query_seconds`,
   `rtc_cache_op_seconds`, `rtc_http_5xx_total`).
2. Inspect logs by `X-Request-Id`. Identify the failing dependency.
3. Scale out or fail over the dependency (RDS Multi-AZ, ElastiCache).
4. If a bad release: `rollback.sh`.

**Incident: database outage**
1. `/health/ready` returns 503 → LB drains instances automatically.
2. Fail over RDS (Multi-AZ) or restore from snapshot/backup.
3. Confirm readiness returns 200; traffic resumes.

**Routine maintenance**
- Weekly `scripts/db/maintenance.sh all` (vacuum/reindex/health).
- Verify a backup restore monthly.
- Review metrics/alerts and capacity headroom.

## Validation

- **Build/tests** — `.github/workflows/ci.yml` (with a Postgres service) runs
  unit + database integration tests and coverage on every push/PR.
- **Docker** — `.github/workflows/docker.yml` builds the production image on
  every PR (and pushes on main/tags).
- **Migrations** — exercised in CI by the integration tests, which run the
  migration runner against a real PostgreSQL before asserting behaviour.
- **Deployment** — `deploy/aws/health-check.sh` verifies liveness + readiness
  after each deploy/rollback.
