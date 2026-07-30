-- Migration 0012: audit log.
--
-- Append-only record of security- and compliance-relevant actions: sign-in and
-- sign-out, password and profile changes, group membership changes, message
-- deletions, role changes and every administrative action.
--
-- Design notes:
--   * Rows are written from the domain event bus, never inline in a request
--     handler, so a slow or unavailable audit table cannot fail a user action.
--   * `event_id` is UNIQUE: the writer is at-least-once (background dispatch,
--     and potentially a broker redelivery), so the unique key makes the insert
--     idempotent and prevents duplicated history.
--   * `actor_id` is ON DELETE SET NULL rather than CASCADE. Deleting a user must
--     never erase the audit trail of what that user did — that is precisely the
--     history an audit log exists to preserve.
--   * `metadata` is JSONB so each event type can carry its own shape without a
--     migration per event, while still being queryable.

CREATE TABLE IF NOT EXISTS audit_logs (
    id              BIGSERIAL    PRIMARY KEY,
    event_id        VARCHAR(32)  NOT NULL UNIQUE,   -- de-duplication key
    event_type      VARCHAR(64)  NOT NULL,          -- rtc::events::EventType wire name
    actor_id        BIGINT       REFERENCES users(id) ON DELETE SET NULL,
    actor_username  VARCHAR(64),                    -- denormalised: survives user deletion
    target_type     VARCHAR(32),                    -- 'user' | 'conversation' | 'message' | ...
    target_id       VARCHAR(64),
    ip              VARCHAR(64),
    user_agent      VARCHAR(255),
    correlation_id  VARCHAR(64),                    -- request id
    trace_id        VARCHAR(32),                    -- W3C trace id
    metadata        JSONB        NOT NULL DEFAULT '{}'::jsonb,
    occurred_at     TIMESTAMPTZ  NOT NULL DEFAULT now(),  -- when it happened
    created_at      TIMESTAMPTZ  NOT NULL DEFAULT now()   -- when it was persisted
);

-- The audit search API pages newest-first and filters by actor, type, target and
-- time window. These indexes cover those access paths.
CREATE INDEX IF NOT EXISTS ix_audit_logs_occurred     ON audit_logs (occurred_at DESC, id DESC);
CREATE INDEX IF NOT EXISTS ix_audit_logs_actor        ON audit_logs (actor_id, occurred_at DESC);
CREATE INDEX IF NOT EXISTS ix_audit_logs_type         ON audit_logs (event_type, occurred_at DESC);
CREATE INDEX IF NOT EXISTS ix_audit_logs_target       ON audit_logs (target_type, target_id);
CREATE INDEX IF NOT EXISTS ix_audit_logs_correlation  ON audit_logs (correlation_id);

-- Containment queries over the per-event payload (e.g. metadata @> '{"emoji":"x"}').
CREATE INDEX IF NOT EXISTS ix_audit_logs_metadata     ON audit_logs USING GIN (metadata);
