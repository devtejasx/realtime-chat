# 0001. Clean architecture with dependency inversion

**Status:** Accepted
**Date:** 2026-07-24

## Context

A chat backend accumulates integrations: a database, a cache, a broker, object
storage, a push provider, a tracer. Written directly against each one, the
business rules become impossible to test without all of them running, and
impossible to change without touching all of them.

## Decision

Layers with dependencies pointing inward: controllers → services →
repositories, with every crossing expressed as an interface owned by the *inner*
layer. Concrete implementations are bound in a single composition root
(`Application`).

The rule enforced throughout: business logic lives only in services;
repositories contain only database operations; controllers contain only HTTP
handling.

## Alternatives

**Transaction script / fat controllers.** Faster for the first ten endpoints.
The cost appears when a second delivery mechanism arrives — the WebSocket
dispatcher and the REST controllers here call the *same* service methods, which
is only possible because the logic is in neither of them.

**A framework's own structure (Rails-style MVC).** Crow provides no such
opinion, so adopting one would have meant inventing it.

## Consequences

An interface per seam is more files and more indirection. The payoff is
concrete: the test suite substitutes in-memory fakes for every repository, so
~400 tests run in about seven seconds with no database — and the same seams made
Redis, pluggable storage and a durable broker additive rather than invasive.

The cost that is easy to underestimate: a developer must know *where* something
belongs, and getting it wrong is not caught by the compiler.

## Revisit when

Never, for this shape of service. If it became a single-purpose function with one
integration, the indirection would stop paying for itself.
