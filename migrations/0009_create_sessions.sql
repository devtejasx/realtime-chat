-- Migration 0009: sessions.
--
-- Durable record of issued sessions for distributed session management and
-- multi-device logout. The refresh token is stored only as a SHA-256 hash;
-- rotation replaces the hash. A session is valid when revoked_at IS NULL and
-- now() < expires_at. Redis caches active sessions for fast validation; this
-- table is the source of truth.

CREATE TABLE IF NOT EXISTS sessions (
    id                  VARCHAR(64)  PRIMARY KEY,       -- session id (JWT jti)
    user_id             BIGINT       NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    refresh_token_hash  VARCHAR(64)  NOT NULL,
    user_agent          VARCHAR(255),
    ip                  VARCHAR(64),
    created_at          TIMESTAMPTZ  NOT NULL DEFAULT now(),
    last_used_at        TIMESTAMPTZ  NOT NULL DEFAULT now(),
    expires_at          TIMESTAMPTZ  NOT NULL,
    revoked_at          TIMESTAMPTZ
);

-- List a user's sessions and revoke-all; sweep expired sessions.
CREATE INDEX IF NOT EXISTS ix_sessions_user    ON sessions (user_id);
CREATE INDEX IF NOT EXISTS ix_sessions_expires ON sessions (expires_at);
