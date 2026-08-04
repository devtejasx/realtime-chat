# 0008. Circuit breaker wrapping retry

**Status:** Accepted
**Date:** 2026-08-04

## Context

When a dependency stops answering, every request that touches it blocks until its
timeout, holding a worker thread. With enough traffic the pool fills with threads
waiting on something that is not going to answer, and endpoints that never touch
that dependency start timing out too. The database being slow becomes the whole
service being down.

## Decision

Two composable primitives, with the **breaker wrapping the retry**:

- `RetryPolicy` — bounded exponential backoff with jitter, for transient blips.
- `CircuitBreaker` — Closed/Open/HalfOpen on consecutive failures, for outages.

Applied to PostgreSQL (at `BaseRepository::with_transaction`, the single choke
point) and the cache (at `CacheService`).

## Alternatives

**Retry alone.** Actively harmful against an outage: three attempts with backoff
hold the thread three times as long. Retry is for blips; it makes outages worse.

**Breaker alone.** Fails requests that a single immediate retry would have
satisfied — a connection reset during a failover is common and recoverable.

**A library (Hystrix-style).** No established C++ equivalent worth the
dependency; the state machine is about 150 lines.

**Rate-based failure counting.** Needs a window and a minimum request volume to
avoid opening on "1 of 1 failed". Consecutive counting is a simpler contract to
reason about during an incident.

## Consequences

Three decisions that are easy to get wrong and are recorded because of it:

- **The retryable predicate is mandatory, with no default.** Retrying blindly
  re-runs non-idempotent writes — a write that timed out *after* the server
  applied it gets applied twice. The caller knows which failures are transient;
  the reliability layer does not.
- **A domain error counts as a breaker success.** A duplicate username means the
  database answered correctly and promptly. Counting it as a dependency failure
  would let ordinary user input trip the breaker.
- **A breaker, not a retry, at the database seam.** A breaker never re-executes
  anything, so it is safe over reads and writes alike.

Jitter is on by default: synchronised clients retrying on identical schedules
arrive as a thundering herd exactly when the dependency can least absorb one.

Breakers are per-instance, not shared. A replica that can still reach the
database keeps serving while another cannot.

## Revisit when

A dependency needs bulkheading (a bounded concurrency budget) as well as
breaking, or per-endpoint rather than per-dependency breakers are needed.
