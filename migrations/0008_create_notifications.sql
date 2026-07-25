-- Migration 0008: notifications.
--
-- Durable, per-recipient notification feed. `payload` is a JSONB blob whose
-- shape depends on `type`, keeping the schema stable as new notification kinds
-- are added. `read_at` NULL means unread.

CREATE TABLE IF NOT EXISTS notifications (
    id         BIGSERIAL   PRIMARY KEY,
    user_id    BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    type       VARCHAR(32) NOT NULL,
    payload    JSONB       NOT NULL DEFAULT '{}'::jsonb,
    read_at    TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Feed listing (newest first) and unread counts per user.
CREATE INDEX IF NOT EXISTS ix_notifications_user      ON notifications (user_id, id DESC);
CREATE INDEX IF NOT EXISTS ix_notifications_unread    ON notifications (user_id) WHERE read_at IS NULL;
