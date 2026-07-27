# Monitoring

## Endpoints

| Endpoint        | Purpose                                                    |
| --------------- | ---------------------------------------------------------- |
| `GET /health`   | service/version/environment summary                        |
| `GET /health/live`  | liveness — process is up (dependency-free, cheap)      |
| `GET /health/ready` | readiness — DB + cache reachable; `503` when not      |
| `GET /metrics`  | Prometheus text exposition                                 |

Use `/health/live` for the orchestrator liveness probe and `/health/ready` for
the readiness probe / load-balancer target-group health check so instances are
drained during a dependency outage.

## Metrics

`GET /metrics` exposes (names prefixed `rtc_`):

| Metric                         | Type    | Meaning                            |
| ------------------------------ | ------- | ---------------------------------- |
| `rtc_http_requests_total`      | counter | total HTTP requests                |
| `rtc_http_request_seconds_*`   | summary | request latency (sum/count/avg)    |
| `rtc_http_4xx_total` / `_5xx_total` | counter | error responses by class      |
| `rtc_active_users`             | gauge   | users currently online             |
| `rtc_ws_connections`           | gauge   | live WebSocket sessions            |
| `rtc_cache_hit_ratio`          | gauge   | cache hits / (hits + misses)       |
| `rtc_db_query_seconds`         | gauge   | probed DB round-trip latency       |
| `rtc_cache_op_seconds`         | gauge   | probed cache round-trip latency    |
| `rtc_uploads_total`            | counter | attachments uploaded               |
| `rtc_notifications_total`      | counter | notifications produced             |
| `rtc_background_jobs_pending`  | gauge   | background queue depth             |
| `rtc_process_memory_bytes`     | gauge   | resident memory (Linux)            |
| `rtc_uptime_seconds`           | gauge   | process uptime                     |

DB/cache latency and memory are refreshed by a periodic maintenance job.

## Prometheus scrape config

```yaml
scrape_configs:
  - job_name: realtime-chat
    metrics_path: /metrics
    static_configs:
      - targets: ["app-1:8080", "app-2:8080"]
```

In production `/metrics` is restricted at the Nginx layer to internal CIDRs
(see `nginx/conf.d/realtime-chat.conf`); scrape the app port directly from
within the VPC/overlay network.

## Suggested alerts

| Alert                     | Expression (sketch)                                   |
| ------------------------- | ----------------------------------------------------- |
| Instance down             | `up == 0`                                             |
| Not ready                 | `probe of /health/ready != 200`                       |
| High error rate           | `rate(rtc_http_5xx_total[5m]) > 0`                    |
| Elevated latency          | `rtc_http_request_seconds_avg > 0.5`                  |
| DB latency spike / down   | `rtc_db_query_seconds > 0.1 or rtc_db_query_seconds < 0` |
| Low cache hit ratio       | `rtc_cache_hit_ratio < 0.5`                           |
| Background backlog        | `rtc_background_jobs_pending > 1000`                  |
| Memory pressure           | `rtc_process_memory_bytes > <threshold>`             |

## Logs

Structured JSON logs (`LOG_FORMAT=json`) carry a per-request id (also returned
in the `X-Request-Id` response header and honoured from an inbound header set by
Nginx), enabling correlation across the proxy access log and the application
log. Ship stdout to your aggregator (CloudWatch, Loki, ELK). Nginx access logs
are JSON as well (`json_combined`).

## Dashboards

A Grafana dashboard should chart: request rate & latency, 4xx/5xx, active users,
WebSocket connections, cache hit ratio, DB/cache latency, background queue
depth, and memory — all available from the metrics above.
