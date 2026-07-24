-- Migration 0003: conversations and participants.
--
-- A conversation is either a one-to-one ("direct") chat or a "group" chat.
-- Membership is modelled in conversation_participants (many-to-many between
-- users and conversations) with a per-member role.

CREATE TABLE IF NOT EXISTS conversations (
    id              BIGSERIAL   PRIMARY KEY,
    type            VARCHAR(16) NOT NULL CHECK (type IN ('direct', 'group')),
    name            VARCHAR(128),
    owner_id        BIGINT      REFERENCES users(id) ON DELETE SET NULL,
    -- For direct conversations this holds the canonical "minUserId:maxUserId"
    -- key, giving a DB-level guarantee against duplicate private chats. NULL for
    -- groups (which may legitimately duplicate).
    direct_key      VARCHAR(64),
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    last_message_at TIMESTAMPTZ
);

-- Enforce one direct conversation per unordered user pair.
CREATE UNIQUE INDEX IF NOT EXISTS ux_conversations_direct_key
    ON conversations (direct_key) WHERE direct_key IS NOT NULL;

-- Conversations are commonly listed most-recently-active first.
CREATE INDEX IF NOT EXISTS ix_conversations_last_message_at
    ON conversations (last_message_at DESC NULLS LAST);

DROP TRIGGER IF EXISTS trg_conversations_set_updated_at ON conversations;
CREATE TRIGGER trg_conversations_set_updated_at
    BEFORE UPDATE ON conversations
    FOR EACH ROW EXECUTE FUNCTION set_updated_at();

CREATE TABLE IF NOT EXISTS conversation_participants (
    id                   BIGSERIAL   PRIMARY KEY,
    conversation_id      BIGINT      NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
    user_id              BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role                 VARCHAR(16) NOT NULL DEFAULT 'member'
                             CHECK (role IN ('owner', 'member')),
    joined_at            TIMESTAMPTZ NOT NULL DEFAULT now(),
    -- High-water mark of the last message this participant has read; drives
    -- unread counts and read receipts without a per-message row here.
    last_read_message_id BIGINT
);

-- A user appears at most once per conversation.
CREATE UNIQUE INDEX IF NOT EXISTS ux_participants_conversation_user
    ON conversation_participants (conversation_id, user_id);

-- "List my conversations" and membership checks pivot on user_id.
CREATE INDEX IF NOT EXISTS ix_participants_user
    ON conversation_participants (user_id);
