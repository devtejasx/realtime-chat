# Troubleshooting

Practical fixes for common issues. For architecture see
[Architecture.md](Architecture.md); for deploys see
[Deployment.md](Deployment.md).

## Startup

**Server exits immediately with a configuration error (exit 78).**
Invalid config. In `production` the app refuses the default/short `JWT_SECRET`.
Set a strong secret: `export JWT_SECRET="$(openssl rand -hex 32)"`. Check the
logged message for the offending variable.

**`Failed to open database connection` / pool init fails.**
Postgres is unreachable or credentials are wrong. Verify `DB_HOST/PORT/USER/
PASSWORD`, that Postgres is healthy (`pg_isready`), and that the app can reach
it (in compose, use the service name `postgres`, not `localhost`).

**`Migrations directory not found`.**
Set `MIGRATIONS_DIR` (the image sets `/app/migrations`). For native runs point
it at the repo's `migrations/`.

## Runtime

**`/health/ready` returns 503.**
A dependency is down — the JSON body's `checks` shows which (`database`/`cache`).
Fix the dependency; the load balancer will resume routing once it returns 200.

**Requests return 429.**
Rate limit hit. Tune `RATE_LIMIT_*` or back off. Limits are per user (when
authenticated) or per client IP.

**Uploads fail with 413.**
File exceeds the limit. Raise `MAX_UPLOAD_BYTES` **and** Nginx
`client_max_body_size` (keep them in sync).

**Uploads fail with 415.**
Content type not on the allow-list, or the extension doesn't match the declared
type. See [Security.md](Security.md) for the accepted types.

**Redis not being used despite `REDIS_ENABLED=true`.**
The binary must be built with `-DRTC_WITH_REDIS=ON` (the production image is).
Without it, or if Redis is unreachable at startup, the app logs the fallback and
uses the in-memory store. Check the startup log line "Using … cache backend".

## WebSocket

**WS upgrade fails / 401 on connect.**
The handshake requires a valid access token via `?token=…` or an
`Authorization: Bearer` header. Ensure Nginx forwards `Upgrade`/`Connection`
headers (it does in the shipped config) and that the token is current.

**Connections drop after ~90s idle.**
Expected: the heartbeat closes idle sockets (`WS_HEARTBEAT_TIMEOUT_SECONDS`).
Clients should respond to server `ping` (or send any frame) to stay alive; Nginx
proxy timeouts for `/ws` are set to 3600s.

## Nginx / TLS

**502 Bad Gateway.**
Nginx can't reach the app. Confirm the `server` container is healthy and on the
`frontend` network, and that the upstream name (`server:8080`) resolves.

**Certificate errors / ACME challenge fails.**
Ensure port 80 is reachable from the internet and `server_name` matches the
domain. Run `scripts/ssl/init-letsencrypt.sh` with the correct `DOMAIN`/`EMAIL`;
use `STAGING=1` first to avoid rate limits.

## Database ops

**Need to roll back a bad migration.**
Migrations are forward-only. Restore the pre-migration backup
(`scripts/db/rollback.sh <backup.dump>`). Always back up before migrating.

**High connection count / "too many clients".**
`instances × DB_POOL_SIZE` exceeds Postgres `max_connections`. Lower the pool,
raise `max_connections`, or add PgBouncer.

## Diagnostics

- Correlate a request across tiers by its `X-Request-Id` (in the response header
  and every log line for that request).
- `GET /metrics` surfaces DB/cache latency, queue depth, and error counters.
- Container logs: `docker compose -f docker-compose.prod.yml logs -f server`.
- systemd logs: `journalctl -u realtime-chat -f`.
