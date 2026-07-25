-- Migration 0006: attachments.
--
-- Stores metadata for uploaded files; the bytes live in the configured storage
-- backend (local filesystem, S3, ...) referenced by `storage_key`. An
-- attachment is uploaded first (message_id NULL) and linked to a message when
-- it is sent, so uploads and message creation stay decoupled.

CREATE TABLE IF NOT EXISTS attachments (
    id                BIGSERIAL    PRIMARY KEY,
    owner_id          BIGINT       NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    message_id        BIGINT       REFERENCES messages(id) ON DELETE CASCADE,
    storage_backend   VARCHAR(32)  NOT NULL DEFAULT 'local',
    storage_key       TEXT         NOT NULL,
    thumbnail_key     TEXT,
    original_filename VARCHAR(255) NOT NULL,
    content_type      VARCHAR(128) NOT NULL,
    kind              VARCHAR(16)  NOT NULL
                          CHECK (kind IN ('image','pdf','document','video','audio','other')),
    byte_size         BIGINT       NOT NULL CHECK (byte_size >= 0),
    width             INTEGER,
    height            INTEGER,
    checksum          VARCHAR(64),
    created_at        TIMESTAMPTZ  NOT NULL DEFAULT now()
);

-- Fetch a message's attachments, and a user's uploads.
CREATE INDEX IF NOT EXISTS ix_attachments_message ON attachments (message_id);
CREATE INDEX IF NOT EXISTS ix_attachments_owner   ON attachments (owner_id);
