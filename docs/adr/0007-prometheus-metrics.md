# 0007. Prometheus pull-based metrics

**Status:** Accepted
**Date:** 2026-07-27

## Context

Traces explain one request; metrics explain the fleet. The service needs
aggregate rates, latencies and resource figures that survive a pod restart and
can be alerted on.

## Decision

A `/metrics` endpoint in Prometheus text exposition format, scraped. Counters,
gauges, render-time gauge callbacks, and histograms with explicit buckets.

## Alternatives

**StatsD / push.** Requires an agent beside every pod and loses the "is this
instance up?" signal that scrape failure gives for free.

**A client library (prometheus-cpp).** A reasonable choice. The registry here is
about 120 lines and has no dependencies, which for counters, gauges and
histograms is genuinely all that is needed — and it made adding render-time
callbacks for live values trivial.

## Consequences

Owning the exposition format means getting it right. One detail that mattered:
`<name>_avg` is emitted as its own gauge family rather than inside the histogram,
because it is not part of the histogram exposition and a strict parser may reject
an unexpected series in a metric family.

Histogram buckets were added late, and their absence had a real cost: with only
`_sum` and `_count`, the only available statistic was an average, and an average
latency hides the tail worth alerting on. Latency uses Prometheus' own default
boundaries so stock dashboards and alert templates work unchanged.

No label support. Series are distinguished by name, which is enough here and
would not be for a service with high-cardinality dimensions.

## Revisit when

Labels are needed — per-endpoint or per-status-code breakdowns would justify
adopting prometheus-cpp rather than growing this registry into one.
