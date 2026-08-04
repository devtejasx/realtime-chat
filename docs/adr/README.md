# Architecture Decision Records

Short documents recording *why* a significant choice was made, written at the
time it was made.

The value is in the rejected alternatives. A codebase shows what was built; it
rarely shows what was considered and discarded, so the same debate gets reopened
every eighteen months by someone who cannot tell whether an option was overlooked
or ruled out. These records answer that.

Each ADR states the context, the decision, the consequences accepted, and — most
importantly — what would have to change for it to be revisited.

| # | Decision | Status |
| --- | --- | --- |
| [0001](0001-clean-architecture.md) | Clean architecture with dependency inversion | Accepted |
| [0002](0002-crow-http-framework.md) | Crow as the HTTP and WebSocket framework | Accepted |
| [0003](0003-postgresql.md) | PostgreSQL as the source of truth | Accepted |
| [0004](0004-redis-pubsub-for-fanout.md) | Redis Pub/Sub for cross-instance fan-out | Accepted |
| [0005](0005-database-authoritative-authorization.md) | Authorization read from the database, not the JWT | Accepted |
| [0006](0006-opentelemetry-tracing.md) | W3C trace context and OTLP export | Accepted |
| [0007](0007-prometheus-metrics.md) | Prometheus pull-based metrics | Accepted |
| [0008](0008-circuit-breaker-and-retry.md) | Circuit breaker wrapping retry | Accepted |
| [0009](0009-durable-message-broker.md) | A durable broker alongside Redis Pub/Sub | Proposed |

## Format

Deliberately short. An ADR nobody reads because it is six pages long has failed
at its only job.

```
# NNNN. Title

**Status:** Proposed | Accepted | Superseded by NNNN
**Date:** YYYY-MM-DD

## Context      what forced a decision
## Decision     what was chosen
## Alternatives what was rejected, and why
## Consequences what this costs, including the bad parts
## Revisit when the conditions that would change the answer
```
