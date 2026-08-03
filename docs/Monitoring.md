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

### Histograms

`rtc_http_request_seconds` and `rtc_upload_bytes` are histograms, exposing
`_bucket{le=...}`, `_sum` and `_count`. The buckets are what make percentiles
possible:

```promql
histogram_quantile(0.99, sum(rate(rtc_http_request_seconds_bucket[5m])) by (le))
```

Latency uses Prometheus' own default boundaries (5ms to 10s) so stock panels and
alert templates work unchanged. Upload size uses byte-shaped boundaries ending
at the 25 MiB cap — against latency boundaries every upload would land in the
same `+Inf` bucket and the histogram would answer nothing.

`<name>_avg` is still emitted for backward compatibility, as its own gauge
family. Prefer `rate(_sum)/rate(_count)`, and prefer a percentile to either: an
average latency hides the tail that matters.

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

Structured JSON logs (`LOG_FORMAT=json`). Every line carries three correlation
fields, always present and empty when there is no context:

```json
{"time":"2026-08-03T09:55:06.541+05:30","level":"info","thread":2480,
 "logger":"realtime-chat","trace_id":"4bf92f...","span_id":"00f067...",
 "request_id":"a3f19c2b","message":"Conversation created"}
```

| Field | Source |
| --- | --- |
| `request_id` | `X-Request-Id` from the client or proxy, else generated. Echoed on the response |
| `trace_id` | The active span's W3C trace id, 32 hex chars |
| `span_id` | The active span, 16 hex chars |

These come from *ambient* context, not from arguments at each call site, so a
log statement written in a repository that knows nothing about HTTP still
carries them. That is deliberate: annotating per call site guarantees the lines
that matter during an incident are the ones nobody remembered to annotate.

An absent trace is emitted as `""` rather than a zeroed id — a zeroed id looks
real and matches nothing.

### Joining logs to a trace

Copy the `trace_id` out of Jaeger and query your log store for it:

```
{service="realtime-chat"} | json | trace_id="4bf92f3577b34da6a3ce929d0e0e4736"
```

That returns every line the request produced, across layers and — because the
id propagates over HTTP headers — across instances. Before these fields existed
the only join key was a timestamp, which is guesswork the moment two requests
overlap.

Text format (`LOG_FORMAT=text`) appends the same ids in brackets, but only when
present, so local development output stays readable.

Ship stdout to your aggregator (CloudWatch, Loki, ELK). Nginx access logs are
JSON as well (`json_combined`) and carry the same `X-Request-Id`.

## Dashboards

[`deploy/grafana/dashboards/realtime-chat-overview.json`](../deploy/grafana/dashboards/realtime-chat-overview.json)
— 26 panels in six rows: golden signals, HTTP traffic, WebSocket/realtime,
cluster health, PostgreSQL/cache, resources/telemetry.

Every query uses a metric this service actually emits; that is verified against
the source rather than assumed. A panel querying a metric that does not exist
renders empty, and an empty panel during an incident reads as "no traffic"
rather than "wrong query".

### Running it locally

```bash
docker compose -f docker-compose.cluster.yml -f docker-compose.observability.yml up --build
```

| | |
| --- | --- |
| Grafana | <http://localhost:3000> (opens on the dashboard) |
| Prometheus | <http://localhost:9090> |
| Jaeger | <http://localhost:16686> |

It overlays the *cluster* stack rather than the single-server one on purpose:
the cluster panels are meaningless with one replica, and per-instance WebSocket
counts need more than one instance to say anything.

Prometheus scrapes both replicas directly rather than through the balancer. Via
the balancer every sample would carry identical labels, collapsing the
per-instance series — and "published on A, received on B" is only visible when A
and B are distinct targets.

Datasource and dashboards are provisioned from files. Edits made in the Grafana
UI are reverted on reload: export the JSON back into
`deploy/grafana/dashboards/` to keep a change, or it disappears with the pod.

### Reading the panels

| Panel | What a bad value means |
| --- | --- |
| Error ratio (5xx) | Counts server errors only — 4xx is excluded, since a client sending bad input is not the service being unhealthy |
| Latency percentiles | Mean is plotted next to p99 deliberately; the gap is why buckets exist |
| Cluster messages/sec | Published climbing while received stays flat means a dead bus, or instances on different Redis databases |
| Spans/sec | A non-zero `dropped` means the exporter queue is saturated and traces will arrive incomplete |
| Uptime | A sawtooth is a crash loop |
| DB query probe | `-1` is a failed probe — connectivity, not a slow query |

## Kubernetes

`deploy/k8s/deployment.yaml` already carries the scrape annotations, so a
cluster-wide Prometheus discovers pods without extra configuration:

```yaml
prometheus.io/scrape: "true"
prometheus.io/port: "8080"
prometheus.io/path: "/metrics"
```

`deploy/prometheus/prometheus.yml` is for the local Compose stack only and is
not used in-cluster. If you run the Prometheus Operator, add a `ServiceMonitor`
selecting `app.kubernetes.io/name: realtime-chat` on the `http` port — the
annotations and a ServiceMonitor are alternatives, not complements, and enabling
both scrapes every pod twice.
