# 0009. A durable broker alongside Redis Pub/Sub

**Status:** Proposed — abstraction landed, AMQP transport not yet implemented
**Date:** 2026-08-04

## Context

Deferred work — push notifications, audit writes, thumbnail rendering, analytics
— currently runs on the in-process `BackgroundExecutor`. Anything queued there is
lost when the pod terminates, and under Kubernetes pods terminate routinely:
rolling deploys, autoscaling, node drains, spot reclamation.

`Application::stop()` drains what it can, but a `SIGKILL` after the termination
grace period does not wait.

Redis Pub/Sub does not solve this. It is at-most-once by design, which is
correct for realtime frames (see [0004](0004-redis-pubsub-for-fanout.md)) and
wrong for work whose *completion* matters more than its latency.

## Decision

Add `IMessageBroker` as a distinct seam for durable, at-least-once work.
**Redis Pub/Sub is not replaced** — the two carry different traffic:

| | Carries | Guarantee | Loss |
| --- | --- | --- | --- |
| Redis Pub/Sub | Realtime fan-out, presence, typing | At-most-once | Acceptable |
| `IMessageBroker` | Email, push, audit, thumbnails, analytics | At-least-once | Not acceptable |

Chat messages, WebSocket broadcasts, presence and typing indicators stay on
Redis. Moving them to a durable broker would add latency to the one path where
latency is the product.

The broker is optional through configuration. `NullMessageBroker` is selected
when none is configured and the service runs exactly as before — this is a
durability tier, not a new startup dependency.

## Alternatives

**Kafka.** Built for replayable event streams and high-throughput analytics
pipelines. Overkill for a work queue, and operationally heavier than the problem.

**A PostgreSQL-backed queue (`SELECT ... FOR UPDATE SKIP LOCKED`).** Genuinely
tempting: no new infrastructure, transactional with the write that enqueues.
Rejected because it puts polling load on the primary that already serves every
read, and reimplements retry, dead-lettering and visibility timeouts. Worth
revisiting if operating a broker proves the greater cost.

**NATS JetStream.** Lighter than RabbitMQ. RabbitMQ preferred for per-message ack
semantics and dead-letter exchanges being first-class rather than configured.

**Keep using BackgroundExecutor.** The status quo, and honest for a single
instance. It loses work on termination, which is precisely the gap.

## Consequences

At-least-once means **handlers must be idempotent** — a redelivery after a crash
between handling and acknowledgement is normal, not exceptional. Stated in the
interface rather than left implicit.

`NullMessageBroker::publish` returns `false` rather than `true`. Reporting
success for a discarded message would surface days later as "the emails never
arrived", with nothing in the logs.

A broker is another piece of infrastructure to run, secure, monitor and upgrade.
That cost is real and is the reason this is scoped to work that genuinely needs
durability.

## Status and what remains

The abstraction, the delivery semantics (retry ladder, dead-lettering, poison
handling, draining shutdown) and 16 tests are landed. `InMemoryMessageBroker`
implements those semantics above the transport, so a developer without RabbitMQ
exercises the real path and the logic that carries the bugs is tested without a
broker running.

**The AMQP transport is not implemented.** It needs a native dependency
(rabbitmq-c or AMQP-CPP) wired through CMake, and integrating one typically takes
several build iterations — which is not safe to attempt while the local
toolchain is unavailable and every iteration costs a full CI round-trip. The
adapter has one job: move bytes and map acknowledgements onto `Ack`.

## Revisit when

The AMQP adapter lands, at which point this becomes Accepted; or if operating
RabbitMQ proves costlier than the PostgreSQL-backed queue above.
