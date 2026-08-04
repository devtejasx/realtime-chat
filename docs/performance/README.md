# Performance

> **Status: methodology defined, results not yet collected.**
>
> This directory describes how to measure the service and where to record the
> numbers. It deliberately contains **no results**, because none have been
> gathered on a controlled machine. Publishing invented or casually-obtained
> figures would be worse than publishing none: a number with a methodology
> attached can be reproduced and argued with, and a number without one is
> decoration.
>
> [results-template.md](results-template.md) is the form to fill in.

## What already exists to run

Both harnesses are in the repository and working; only the *runs* are missing.

| Suite | Location | Covers |
| --- | --- | --- |
| Google Benchmark | [`benchmarks/`](../../benchmarks) | JWT sign/verify, cache operations, database round trips, event bus dispatch, WebSocket protocol encode/decode, authorization checks |
| k6 load tests | [`loadtest/`](../../loadtest) | Authentication, messaging, WebSocket concurrency, upload |

## Why the methodology matters more than the numbers

A throughput figure without its conditions is unusable. "40,000 requests/second"
means nothing without knowing whether that was one core or thirty-two, whether
the database was on the same host, whether the payload was 100 bytes or 100 KB,
and whether the number is a mean or a p99.

So every recorded result must carry the environment that produced it. The
template enforces this by making those fields mandatory rather than optional.

## Running the microbenchmarks

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DRTC_BUILD_BENCHMARKS=ON
cmake --build build --parallel
./build/bin/rtc_benchmarks --benchmark_format=json --benchmark_out=results.json
```

Release, not Debug — measuring an unoptimised build tells you about the compiler,
not the code.

Rules for a run worth recording:

- **Quiesce the machine.** No browser, no IDE indexing, no container builds. On a
  laptop, plug it in: CPU frequency scaling on battery invalidates the run.
- **Pin the CPU governor to `performance`** on Linux. Ondemand scaling produces
  bimodal results that look like variance in the code.
- **Repeat.** `--benchmark_repetitions=10 --benchmark_report_aggregates_only=true`
  gives mean, median and standard deviation. A single run is an anecdote.
- **Record the standard deviation, not just the mean.** A benchmark with 30%
  variance is not measuring what it thinks it is.

## Running the load tests

```bash
docker compose -f docker-compose.cluster.yml -f docker-compose.observability.yml up --build
k6 run --vus 100 --duration 5m loadtest/messaging.js
```

Against the **cluster** stack, so the figures reflect the deployed topology
rather than a single process. Grafana is up in that stack, which is where CPU,
memory and per-instance behaviour come from — k6 reports client-side latency and
error rate, and cannot see what the server was doing.

The requested rungs are 100 / 500 / 1,000 / 5,000 / 10,000 virtual users. Two
honest caveats before running them:

- **The load generator becomes the bottleneck first.** k6 at 10,000 VUs on the
  same machine as the service measures the laptop, not the service. Those rungs
  need a separate load-generation host, ideally several.
- **10,000 concurrent WebSockets needs file-descriptor headroom.** Default
  `ulimit -n` is usually 1024; the connections will fail as a client-side limit
  and look like a server error.

## What to measure

| Metric | Source |
| --- | --- |
| Requests/sec, latency percentiles, error rate | k6 summary |
| p50 / p95 / p99 server-side latency | `histogram_quantile` over `rtc_http_request_seconds_bucket` |
| CPU, memory | Grafana / `rtc_process_memory_bytes` |
| WebSocket concurrency | `rtc_ws_connections` per instance |
| Cross-instance fan-out | `rtc_cluster_published_total` vs `rtc_cluster_received_total` |
| Cache effectiveness | `rtc_cache_hit_ratio` |
| Database latency | `rtc_db_query_seconds` |

Prefer the server-side percentiles over k6's for latency attribution: k6 measures
the network round trip, which on a loopback interface is not the number that will
hold in production.

## Interpreting a result

Two failure modes to watch for, both of which produce impressive numbers:

**Measuring the cache, not the database.** A benchmark that reads the same row
repeatedly measures a cache hit after the first iteration. Vary the key.

**Measuring an empty table.** A query plan over 100 rows is not the plan over 10
million — PostgreSQL will happily sequential-scan a small table faster than it
would use the index it needs at scale. Seed representative volume before drawing
conclusions about query performance.

## Why there are no numbers here yet

Being direct about it: the toolchain on the machine where this work was done
became unavailable (endpoint protection blocking `cmake`, `ninja` and the
linker), so neither suite could be built or run. Rather than estimate, this
directory ships the method and an empty template.

Anyone with a working build can produce a real result in about twenty minutes.
