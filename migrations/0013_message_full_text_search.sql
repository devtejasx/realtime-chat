-- Migration 0013: proper full-text search over messages.
--
-- What was here before: migration 0004 created
--     GIN (to_tsvector('simple', content))
-- and the list() query computed to_tsvector('simple', content) per row at query
-- time. That works, but leaves real capability on the table:
--
--   * 'simple' does no stemming, so a search for "running" misses "runs".
--   * An expression index is only usable when the query's expression matches it
--     *exactly*, which is fragile — one differing argument and the planner
--     silently falls back to a sequential scan over every message.
--   * There is no ranking, no snippet highlighting, and no tolerance for typos.
--
-- This migration adds a STORED GENERATED column, so the vector is computed once
-- on write instead of on every row of every search, and indexes it. Weighting
-- gives future headroom (an 'A' section for a subject line, say) without another
-- migration.
--
-- Backward compatibility: the old index and the old query path are left in place
-- and keep working. Nothing that exists today changes behaviour; the new columns
-- back the new search API only.

-- --------------------------------------------------------------------------
-- Trigram support, for fuzzy matching and typo tolerance.
--
-- CREATE EXTENSION needs elevated rights. Rather than making the whole migration
-- fail on a locked-down managed instance (RDS grants rds_superuser, but a
-- least-privilege setup may not), the failure is caught and downgraded to a
-- notice. Fuzzy search then degrades to exact full-text matching, which the
-- application handles: see PgMessageSearchRepository, which probes for the
-- extension rather than assuming it.
-- --------------------------------------------------------------------------
DO $$
BEGIN
    CREATE EXTENSION IF NOT EXISTS pg_trgm;
EXCEPTION
    WHEN insufficient_privilege THEN
        RAISE NOTICE 'pg_trgm not installed (insufficient privilege); '
                     'fuzzy search will be disabled at runtime';
END $$;

-- --------------------------------------------------------------------------
-- Stored search vector.
--
-- setweight(..., 'B') marks this as body text. to_tsvector(regconfig, text) with
-- a literal config is IMMUTABLE, which is what makes it legal in a generated
-- column; the single-argument form is only STABLE and would be rejected.
-- --------------------------------------------------------------------------
ALTER TABLE messages
    ADD COLUMN IF NOT EXISTS search_vector tsvector
        GENERATED ALWAYS AS (setweight(to_tsvector('english', content), 'B')) STORED;

-- Primary search index.
CREATE INDEX IF NOT EXISTS ix_messages_search_vector
    ON messages USING GIN (search_vector);

-- Fuzzy / similarity index over the raw text. GIN with gin_trgm_ops supports
-- both ILIKE '%term%' and the similarity operator, so one index serves
-- substring matching and typo tolerance.
DO $$
BEGIN
    CREATE INDEX IF NOT EXISTS ix_messages_content_trgm
        ON messages USING GIN (content gin_trgm_ops);
EXCEPTION
    WHEN undefined_object THEN
        RAISE NOTICE 'gin_trgm_ops unavailable; skipping trigram index';
END $$;

-- Search is always scoped to the caller's conversations, so the hot predicate is
-- (conversation_id, deleted_at IS NULL) with a recency order. A partial index
-- excluding deleted rows keeps it small — deleted messages are never searchable.
CREATE INDEX IF NOT EXISTS ix_messages_search_scope
    ON messages (conversation_id, created_at DESC)
    WHERE deleted_at IS NULL;

-- --------------------------------------------------------------------------
-- User directory search (username / display name), same trigram treatment.
-- --------------------------------------------------------------------------
DO $$
BEGIN
    CREATE INDEX IF NOT EXISTS ix_users_username_trgm
        ON users USING GIN (username gin_trgm_ops);
    CREATE INDEX IF NOT EXISTS ix_users_display_name_trgm
        ON users USING GIN (COALESCE(display_name, '') gin_trgm_ops);
EXCEPTION
    WHEN undefined_object THEN
        RAISE NOTICE 'gin_trgm_ops unavailable; skipping user trigram indexes';
END $$;
