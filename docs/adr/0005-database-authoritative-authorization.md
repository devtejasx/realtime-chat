# 0005. Authorization read from the database, not the JWT

**Status:** Accepted
**Date:** 2026-07-27

## Context

Every request needs the caller's role and ban status. The obvious optimisation is
to put both in the access token, making authorization free.

## Decision

Roles and ban state are read from the database on every authorization decision,
behind a 30-second cache.

## Alternatives

**Role claims in the JWT.** What most tutorials do, and free at request time. It
is also wrong for anything revocable: a token is a bearer credential valid until
it expires, so a demoted moderator or a banned user keeps their old rights for
the remainder of the access TTL. "Banned" that takes effect in fifteen minutes is
not banned.

**A token denylist.** Solves revocation, but needs a lookup per request anyway —
the same cost as reading the role, plus another structure to maintain and expire.

## Consequences

One cache hit per authorization rather than zero. The TTL is the worst-case
staleness if an invalidation is ever missed.

This decision is what forced cache invalidation to become cluster-wide. With
replicas, a ban applied through one pod left the other two honouring their cached
role until it expired — so the guarantee held on a single instance and quietly
failed on three. Invalidation is now published on `rtc:cluster:cache`.

There is a second-order trap worth recording: the database circuit breaker must
treat a *domain* error (duplicate username, missing row) as a success. Counting
those as dependency failures would let a burst of conflicting registrations trip
the breaker and take the database offline for everyone.

## Revisit when

Authorization becomes a measured bottleneck. It is currently a cache hit.
