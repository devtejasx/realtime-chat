-- Migration 0002: add profile fields to users.
--
-- Extends the Phase 1 identity table with optional public profile attributes.
-- All columns are nullable so existing rows remain valid; the application
-- treats NULL as "unset" and serialises it accordingly.

ALTER TABLE users
    ADD COLUMN IF NOT EXISTS display_name VARCHAR(64),
    ADD COLUMN IF NOT EXISTS bio          VARCHAR(500),
    ADD COLUMN IF NOT EXISTS avatar_url   VARCHAR(2048);
