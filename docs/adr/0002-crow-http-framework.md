# 0002. Crow as the HTTP and WebSocket framework

**Status:** Accepted
**Date:** 2026-07-24

## Context

The service needs HTTP and WebSockets in one process, sharing authentication and
routing, in C++20.

## Decision

Crow, header-only, with standalone Asio.

## Alternatives

**Boost.Beast.** More capable and far better maintained, but it is a protocol
library rather than a framework: routing, middleware and WebSocket lifecycle
would all have been written here. That is the right choice for a proxy and the
wrong one for an application.

**Drogon.** The closest competitor, with a real ORM and better published
throughput. Rejected mainly on build weight — Crow is header-only and fetched by
CMake with no system packages, which keeps `docker compose up` the entire setup
story.

**Pistache, oat++.** Smaller communities; oat++ imposes its own object mapping.

## Consequences

Crow is a small project with real rough edges, and two have already cost time
here:

- Routes are resolved in `handle_url()` **before** any middleware runs, so a
  middleware cannot rewrite a path. That misconception made every
  `/api/v1/...` request return 404 until routes were registered under both
  prefixes.
- `crow::response::get_header_value()` is non-const, and middleware contexts must
  be move-assignable — both constrain otherwise-natural designs, the second
  ruling out an RAII scope guard in `LoggingMiddleware`.

The framework is contained behind `rtc::http::App` and the controllers, so
replacing it would be a large but bounded change.

## Revisit when

Crow stops being maintained, or the connection layer becomes a measured
bottleneck. Neither is true today, and the second should be measured rather than
assumed.
