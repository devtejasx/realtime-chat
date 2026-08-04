# 0003. PostgreSQL as the source of truth

**Status:** Accepted
**Date:** 2026-07-24

## Context

Chat data is relational — users, conversations, memberships, messages, reactions,
receipts — and the read patterns are joins. It also needs full-text search over
message bodies.

## Decision

PostgreSQL 16 via libpqxx, with a connection pool and SQL migrations owned by
this repository.

## Alternatives

**MongoDB.** Appealing for message documents right up until membership and
permission checks arrive, at which point the joins move into application code —
slower, and race-prone without transactions.

**A separate search engine (Elasticsearch, Meilisearch).** PostgreSQL full-text
with `ts_rank_cd` ranking, `ts_headline` highlighting and a trigram fuzzy
fallback covers the requirement without a second datastore to run, secure and
keep in sync. A sync pipeline is where "search returns a deleted message" bugs
come from.

**An ORM.** Rejected in favour of explicit SQL. The queries are the
performance-critical part of this service, and hiding them behind a generator
turns `EXPLAIN ANALYZE` into archaeology.

## Consequences

Schema changes require migrations, and the migration runner is code we maintain.
Full-text search shares the primary's resources; at volume it would contend with
transactional load.

Writing SQL by hand means writing it correctly by hand — the mitigation is that
every query is visible and reviewable in one place per repository.

## Revisit when

Full-text search measurably contends with transactional load (move to a replica
or a dedicated engine), or write volume exceeds one primary.
