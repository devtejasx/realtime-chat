# Performance results — <date>

Copy this file to `results-YYYY-MM-DD.md` and fill it in. Leave a field blank
rather than guessing: an empty cell is information, an invented one is not.

## Environment

Every number below is meaningless without this section, which is why it comes
first.

| | |
| --- | --- |
| Date | |
| Commit | `git rev-parse --short HEAD` |
| Build type | Release / Debug (Debug results are not comparable) |
| Compiler | e.g. GCC 15.2.0 |
| CPU | model, physical cores, threads |
| Memory | |
| Storage | NVMe / SSD / network-attached — this dominates database latency |
| OS / kernel | |
| CPU governor | `performance` / `ondemand` |
| Topology | single process / 2-instance cluster / Kubernetes |
| PostgreSQL | version, same host or remote, connection-pool size |
| Redis | version, same host or remote |
| Load generator | same host or separate — **state this**, it decides whether the numbers are about the service or the laptop |
| Dataset | rows in `messages`, `users`, `conversations` |

## Microbenchmarks

`./build/bin/rtc_benchmarks --benchmark_repetitions=10 --benchmark_report_aggregates_only=true`

| Benchmark | Mean | Median | Stddev | Iterations | Notes |
| --- | --- | --- | --- | --- | --- |
| JWT sign | | | | | |
| JWT verify | | | | | |
| Cache get (hit) | | | | | |
| Cache get (miss) | | | | | |
| Cache set | | | | | |
| Authorization check (cached) | | | | | |
| Authorization check (cold) | | | | | |
| Event bus dispatch | | | | | |
| Protocol encode v1 | | | | | |
| Protocol encode v2 | | | | | |
| Protocol decode | | | | | |
| Database round trip | | | | | |

A stddev above ~10% of the mean means the machine was not quiet. Re-run rather
than record it.

## Load tests

One table per rung. `k6 run --vus N --duration 5m loadtest/<scenario>.js`

### N virtual users — <scenario>

| Metric | Value |
| --- | --- |
| Requests/sec | |
| p50 latency (k6, client-side) | |
| p95 latency (k6) | |
| p99 latency (k6) | |
| p95 latency (server, `rtc_http_request_seconds_bucket`) | |
| Error rate | |
| 5xx count | |
| Peak CPU (per instance) | |
| Peak RSS (per instance) | |
| Peak `rtc_ws_connections` | |
| `rtc_cache_hit_ratio` at steady state | |
| Max `rtc_db_query_seconds` | |
| `rtc_cluster_published_total` / `received_total` | |

Record where it **stopped** scaling and what the limiting resource was — that is
the useful finding. "It handled 500 users" says less than "it saturated at 1,200
because the connection pool was the ceiling."

## Observations

Free text. What was the bottleneck? What surprised you? What would you change
first?

## Known distortions

Note anything that makes these numbers optimistic or pessimistic:

- [ ] Load generator on the same host as the service
- [ ] Database on the same host
- [ ] Small dataset (query plans differ at scale)
- [ ] Loopback networking (no real latency or packet loss)
- [ ] `ulimit -n` not raised for high WebSocket counts
- [ ] Tracing sample ratio at 1.0 (production would be far lower)
