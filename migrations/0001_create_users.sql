-- Migration 0001: create the users table.
--
-- Establishes the core identity table for authentication. Usernames and emails
-- are unique and case-normalised by the application before insertion. Timestamps
-- are timezone-aware; updated_at is maintained by a trigger so the database is
-- the single source of truth for modification time.

CREATE TABLE IF NOT EXISTS users (
    id            BIGSERIAL    PRIMARY KEY,
    username      VARCHAR(32)  NOT NULL,
    email         VARCHAR(254) NOT NULL,
    password_hash TEXT         NOT NULL,
    created_at    TIMESTAMPTZ  NOT NULL DEFAULT now(),
    updated_at    TIMESTAMPTZ  NOT NULL DEFAULT now()
);

-- Enforce uniqueness case-insensitively to prevent "Alice" and "alice" from
-- registering as distinct accounts. The application also lower-cases emails.
CREATE UNIQUE INDEX IF NOT EXISTS ux_users_username_lower ON users (LOWER(username));
CREATE UNIQUE INDEX IF NOT EXISTS ux_users_email_lower    ON users (LOWER(email));

-- Trigger function: keep updated_at in sync on every UPDATE.
CREATE OR REPLACE FUNCTION set_updated_at()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = now();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_users_set_updated_at ON users;
CREATE TRIGGER trg_users_set_updated_at
    BEFORE UPDATE ON users
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
