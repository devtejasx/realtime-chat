-- Migration 0005: read receipts.
--
-- Tracks per-user delivery/read state for a message. State advances monotonically
-- sent -> delivered -> read. One row per (message, user); the unique index makes
-- upserts idempotent.

CREATE TABLE IF NOT EXISTS read_receipts (
    id         BIGSERIAL   PRIMARY KEY,
    message_id BIGINT      NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
    user_id    BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    state      VARCHAR(16) NOT NULL DEFAULT 'sent'
                   CHECK (state IN ('sent', 'delivered', 'read')),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX IF NOT EXISTS ux_read_receipts_message_user
    ON read_receipts (message_id, user_id);

-- "What is the receipt state for these messages" and per-user lookups.
CREATE INDEX IF NOT EXISTS ix_read_receipts_message ON read_receipts (message_id);
CREATE INDEX IF NOT EXISTS ix_read_receipts_user    ON read_receipts (user_id);

DROP TRIGGER IF EXISTS trg_read_receipts_set_updated_at ON read_receipts;
CREATE TRIGGER trg_read_receipts_set_updated_at
    BEFORE UPDATE ON read_receipts
    FOR EACH ROW EXECUTE FUNCTION set_updated_at();
