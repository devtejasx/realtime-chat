-- Migration 0010: cache metadata.
--
-- Bookkeeping for cache coherence across instances. Each logical cache entry
-- carries a monotonically increasing version; bumping the version on a write
-- lets other instances detect stale reads and drives cache-warming decisions.

CREATE TABLE IF NOT EXISTS cache_metadata (
    cache_key   VARCHAR(255) PRIMARY KEY,
    namespace   VARCHAR(64)  NOT NULL,
    version     BIGINT       NOT NULL DEFAULT 1,
    updated_at  TIMESTAMPTZ  NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS ix_cache_metadata_namespace ON cache_metadata (namespace);
