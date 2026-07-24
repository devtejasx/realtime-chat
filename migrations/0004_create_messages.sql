-- Migration 0004: messages.
--
-- Messages belong to a conversation and a sender. Edits set edited_at; deletes
-- are soft (deleted_at) so history and receipts remain consistent. A GIN
-- full-text index backs keyword search.

CREATE TABLE IF NOT EXISTS messages (
    id              BIGSERIAL   PRIMARY KEY,
    conversation_id BIGINT      NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
    sender_id       BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    type            VARCHAR(16) NOT NULL DEFAULT 'text' CHECK (type IN ('text', 'system')),
    content         TEXT        NOT NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    edited_at       TIMESTAMPTZ,
    deleted_at      TIMESTAMPTZ
);

-- Primary access pattern: fetch a conversation's messages newest-first with
-- keyset pagination on id. A composite index serves both filter and order.
CREATE INDEX IF NOT EXISTS ix_messages_conversation_id
    ON messages (conversation_id, id DESC);

-- Search/filter by sender.
CREATE INDEX IF NOT EXISTS ix_messages_sender
    ON messages (sender_id);

-- Full-text keyword search over message content.
CREATE INDEX IF NOT EXISTS ix_messages_content_fts
    ON messages USING GIN (to_tsvector('simple', content));

DROP TRIGGER IF EXISTS trg_messages_set_updated_at ON messages;
CREATE TRIGGER trg_messages_set_updated_at
    BEFORE UPDATE ON messages
    FOR EACH ROW EXECUTE FUNCTION set_updated_at();
