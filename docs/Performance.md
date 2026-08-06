# Performance

Design targets: **100k+ registered users** and **10k+ concurrent WebSocket
connections** per deployment, scaling horizontally beyond that.

## Database

- **Connection pooling** — a fixed, thread-safe pool (`DB_POOL_SIZE`) leased per
  request via RAII; sized so `instances × pool_size` stays within Postgres
  `max_connections`.
- **Fixed-shape, parameterised statements** — list/search uses a single query
  with optional predicates (`$n IS NULL OR …`), so Postgres plans/prepares it
  once and avoids query-plan churn.
- **Indexes** for every hot path: `(conversation_id, id DESC)` for message
  paging, a **GIN** index for full-text search, partial unique indexes for
  direct-chat dedup and unread notifications. See [Database.md](Database.md).
- **Atomic side effects** — sending a message inserts the row and advances the
  conversation's `last_message_at` in one transaction.
- **Keyset pagination** (`before`/`after` on id) stays fast at any depth, unlike
  large `OFFSET`s.
- Routine `VACUUM (ANALYZE)` and `REINDEX CONCURRENTLY` via
  `scripts/db/maintenance.sh`.

## Redis / cache

- `ICacheStore` abstraction with an in-memory default and a Redis backend for
  multi-instance sharing.
- `CacheService` provides read-through caching with **hit/miss metrics**;
  entries carry TTLs, and invalidation is explicit at write sites.
- Redis is configured with `maxmemory` + `allkeys-lru` in the prod compose so it
  bounds memory and evicts cold keys.
- Rate limiting uses atomic `INCR` + create-TTL — one round-trip per check.

## WebSocket

- **Dual-indexed** session registry (`connection ↔ user`) for O(1) lookup and
  fan-out to all of a user's devices.
- **RoomManager** routes high-frequency signals (typing/presence) to a
  conversation's *online* members without a DB hit.
- Broadcast payloads are **serialised once** and delivered to every target
  session.
- A **HeartbeatMonitor** closes idle sockets, bounding resource use.

## Application

- **Thread pools**: Crow serves on a worker pool; a separate `BackgroundExecutor`
  moves thumbnailing and push delivery **off the request path**; a
  `PeriodicScheduler` runs maintenance.
- **Move semantics** and `string_view` throughout to avoid needless copies;
  services are stateless beyond injected collaborators and safe to share.
- **Persist-first, broadcast-second** guarantees durability without holding
  request threads on delivery.

## Tuning knobs

| Setting                       | Effect                                   |
| ----------------------------- | ---------------------------------------- |
| `DB_POOL_SIZE`                | DB concurrency per instance              |
| `WORKER_THREADS`              | background job parallelism               |
| `WS_HEARTBEAT_*`              | idle-connection reclamation              |
| `RATE_LIMIT_*`                | protection vs. throughput                |
| Redis `maxmemory` / policy    | cache footprint and eviction             |
| Nginx `worker_connections`    | edge connection capacity                 |

## Scaling out

Run **N stateless app instances** behind a load balancer with shared Postgres
and Redis. Presence, cache, sessions, and rate limits are shared via Redis, so
any instance serves any request. Scale Postgres with read replicas and
connection poolers (PgBouncer) as needed.

## Measuring

Watch `rtc_http_request_seconds_avg`, `rtc_db_query_seconds`,
`rtc_cache_hit_ratio`, `rtc_ws_connections`, and `rtc_background_jobs_pending`
(see [Monitoring.md](Monitoring.md)). Load-test WebSocket fan-out and message
throughput before capacity planning.

The targets at the top of this page are design goals, not measurements. For
figures that were actually taken — and the conditions that produced them — see
[performance/results-2026-08-06.md](performance/results-2026-08-06.md). The short
version from that run: messaging is bounded by PostgreSQL and authentication by
bcrypt, which are separate limits with separate remedies, and neither is the
application layer.
