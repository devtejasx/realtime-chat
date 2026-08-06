# 0006. W3C trace context and OTLP export

**Status:** Accepted
**Date:** 2026-07-30

## Context

With multiple instances, a background worker pool and a cross-instance bus, "why
was this request slow?" cannot be answered from one process's logs.

## Decision

W3C `traceparent` propagation, spans around HTTP handling, repositories, cache
operations and WebSocket events, exported over OTLP/HTTP with a Zipkin wire
format as an alternative.

Trace context is carried explicitly across the two boundaries where the
thread-local cannot follow it: onto the cluster bus envelope as `_traceparent`,
and into the background executor by capturing at `submit()` and restoring on the
worker.

## Alternatives

**The official OpenTelemetry C++ SDK.** The obvious choice, and rejected
reluctantly: it pulls in gRPC and protobuf, which roughly doubles build time and
image size for a service that needs a few thousand spans a minute. The exporter
here speaks the same OTLP/HTTP JSON protocol, so any OTLP collector accepts it.

**Logs only, correlated by request id.** Answers "what happened" but not "where
did the time go", and cannot show a call crossing two instances.

**Vendor SDK (Datadog, New Relic).** Ties the service to one backend. OTLP is
accepted by Jaeger, Tempo, Honeycomb and the OpenTelemetry Collector.

## Consequences

A hand-written exporter is code we own and must keep compatible. It implements
the subset actually used — spans, attributes, status, parent links — not the full
specification.

Sampling is a ratio, so at production ratios most requests carry no context. Every
part of the system degrades to a no-op in that case rather than emitting a zeroed
trace id, which would look real and match nothing.

## Revisit when

Metrics or logs need to flow through OTLP too, or span volume justifies the SDK's
batching and retry machinery.
