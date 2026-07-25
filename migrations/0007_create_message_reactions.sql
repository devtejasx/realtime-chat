-- Migration 0007: message reactions.
--
-- One reaction per user per message; changing a reaction updates the emoji in
-- place (enforced by the unique index). Deleting removes the row.

CREATE TABLE IF NOT EXISTS message_reactions (
    id         BIGSERIAL   PRIMARY KEY,
    message_id BIGINT      NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
    user_id    BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    emoji      VARCHAR(16) NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX IF NOT EXISTS ux_reactions_message_user
    ON message_reactions (message_id, user_id);
CREATE INDEX IF NOT EXISTS ix_reactions_message ON message_reactions (message_id);

DROP TRIGGER IF EXISTS trg_reactions_set_updated_at ON message_reactions;
CREATE TRIGGER trg_reactions_set_updated_at
    BEFORE UPDATE ON message_reactions
    FOR EACH ROW EXECUTE FUNCTION set_updated_at();
