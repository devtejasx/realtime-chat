# 0004. Redis Pub/Sub for cross-instance fan-out

**Status:** Accepted
**Date:** 2026-07-30

## Context

A WebSocket connection is pinned to whichever instance accepted it — that is a
TCP connection to one process, not a design choice. So with more than one
replica, the instance handling a write is usually *not* the one holding the
recipients' sockets.

Nothing errors in that situation. The message is persisted correctly and
delivered to the sender's own connections; the recipient simply never receives
it and finds it only on reload. The failure is invisible in every
single-instance test, which is what makes it dangerous.

## Decision

After delivering locally, each instance republishes the event on a shared Redis
channel; every peer delivers it to its own connections. Loop suppression is by
node id — each message carries the publisher's `RTC_NODE_ID`, and receivers drop
their own.

## Alternatives

**Sticky sessions at the load balancer.** Pins one client to one instance, which
does nothing for the common case: two *different* users on two instances.

**A durable broker for realtime frames.** Wrong tool. Durability costs latency,
and a realtime frame that arrives late is worse than useless — PostgreSQL is
already the record a reconnecting client re-reads. See
[0009](0009-durable-message-broker.md) for where a broker does belong.

**Direct instance-to-instance connections.** Needs service discovery and an
O(n²) mesh, for no benefit at this scale.

## Consequences

At-most-once delivery: an instance that is down when a message is published never
sees it. Accepted, because the durable record is in PostgreSQL.

Redis becomes a single point of failure for cross-instance delivery. While it is
down each instance serves only its own connections — partial delivery rather than
an outage, and reported by `/health/ready` as `"distributed": false`.

The property that keeps one send from becoming two deliveries is loop
suppression, and it is easy to break: the inbound handler must call
`deliver_local`, never `publish`. Tests assert exactly that.

## Revisit when

Ordering guarantees across instances become a requirement, or fan-out volume
exceeds one Redis.
