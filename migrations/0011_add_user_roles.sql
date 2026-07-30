-- Migration 0011: user roles and account suspension (RBAC).
--
-- Adds the authorisation attributes that were previously implicit: every account
-- was effectively a plain user with no way to express moderation or
-- administration rights, and no way to suspend an account short of deleting it.
--
-- Backward compatibility: `role` is NOT NULL with a DEFAULT, so existing rows
-- and any INSERT that predates this migration continue to work unchanged and
-- land on 'user' — the least-privileged value. Nothing gains permissions by
-- being migrated.
--
-- Enforcement is DB-authoritative rather than encoded in the JWT: see
-- rtc::services::AuthorizationService for why (a demotion or ban must take
-- effect immediately, not when the access token happens to expire).

ALTER TABLE users
    ADD COLUMN IF NOT EXISTS role          VARCHAR(20)  NOT NULL DEFAULT 'user',
    ADD COLUMN IF NOT EXISTS banned_at     TIMESTAMPTZ,
    ADD COLUMN IF NOT EXISTS ban_reason    VARCHAR(255),
    ADD COLUMN IF NOT EXISTS banned_by     BIGINT;

-- Constrain the role to the values rtc::security::Role can parse. A row the
-- application cannot interpret would silently fail closed to 'user'; rejecting
-- it at write time surfaces the mistake instead.
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'ck_users_role') THEN
        ALTER TABLE users ADD CONSTRAINT ck_users_role
            CHECK (role IN ('user', 'moderator', 'admin', 'super_admin'));
    END IF;
END $$;

-- Self-referencing FK for the acting administrator. ON DELETE SET NULL keeps the
-- suspension in force if the admin's own account is later removed.
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'fk_users_banned_by') THEN
        ALTER TABLE users ADD CONSTRAINT fk_users_banned_by
            FOREIGN KEY (banned_by) REFERENCES users(id) ON DELETE SET NULL;
    END IF;
END $$;

-- Admin listing filters by role and by suspension state. The partial index only
-- covers suspended accounts, which are the rare case worth indexing.
CREATE INDEX IF NOT EXISTS ix_users_role   ON users (role);
CREATE INDEX IF NOT EXISTS ix_users_banned ON users (banned_at) WHERE banned_at IS NOT NULL;
