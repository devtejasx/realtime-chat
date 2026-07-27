# Docker

realtime-chat ships three Docker build/run configurations.

| File                        | Purpose                                            |
| --------------------------- | -------------------------------------------------- |
| `docker/Dockerfile`         | default multi-stage build (no Redis, minimal deps) |
| `docker/Dockerfile.prod`    | production image: Redis-enabled, tini, healthcheck |
| `docker-compose.dev.yml`    | dev stack (Postgres + Redis + app, port published) |
| `docker-compose.prod.yml`   | prod stack (Postgres + Redis + app + Nginx + certbot) |

## Development

```bash
docker compose -f docker-compose.dev.yml up --build
curl http://localhost:8080/health
```

Human-readable logs, app exposed on `:8080`, Redis enabled. Data persists in the
`postgres_dev_data` / `uploads_dev_data` volumes.

## Production

```bash
export JWT_SECRET="$(openssl rand -hex 32)"
export DB_PASSWORD="$(openssl rand -hex 16)"
docker compose -f docker-compose.prod.yml up -d --build
```

- **App is not published directly** — only Nginx (`80`/`443`) is exposed.
- Postgres, Redis, and the app share an **internal** network (`backend`) that
  never leaves the host; Nginx bridges the `frontend` network.
- JSON logs (`LOG_FORMAT=json`), `APP_ENV=production` (strict secret checks).

### Production image details (`Dockerfile.prod`)

- **Multi-stage**: a build stage compiles the app (and builds
  redis-plus-plus so `-DRTC_WITH_REDIS=ON` links); the runtime stage carries
  only the binary, migrations, and shared libraries.
- **tini** is PID 1 — forwards `SIGTERM` for graceful shutdown and reaps
  children.
- **Non-root** (`appuser`, uid 10001); uploads live on the `/data` volume.
- **HEALTHCHECK** hits `/health/live` so Docker/orchestrators see container
  health.

## Health checks & restart

Every service defines a healthcheck and `restart: unless-stopped`. The app
container additionally exposes `/health/ready` (checks DB + Redis) — use it as
the load-balancer target-group probe so an instance is drained during a
dependency outage.

## Resource limits

`docker-compose.prod.yml` sets `deploy.resources.limits` (CPU/memory) per
service. Tune them to your instance size. On a single host these cap runaway
usage; in Swarm/orchestrators they are enforced as scheduling constraints.

## Migrations

Run migrations as a one-shot before rolling the app (idempotent):

```bash
docker compose -f docker-compose.prod.yml run --rm server --migrate
```

## Volumes

| Volume           | Contents                          |
| ---------------- | --------------------------------- |
| `postgres_data`  | PostgreSQL data directory         |
| `redis_data`     | Redis AOF persistence             |
| `uploads_data`   | uploaded attachments (`/data`)    |
| `certbot_certs`  | Let's Encrypt certificates        |
| `certbot_www`    | ACME webroot challenge files      |

Back these up (see `scripts/db/backup.sh` for the database).

## Building the image manually

```bash
docker build -f docker/Dockerfile.prod -t realtime-chat:local .
docker run --rm -p 8080:8080 --env-file .env realtime-chat:local
```

CI publishes images to `ghcr.io/devtejasx/realtime-chat` on pushes to `main`
and tags (see `.github/workflows/docker.yml`).
