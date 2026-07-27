#!/usr/bin/env bash
# Apply pending schema migrations, then exit — for use ahead of a rolling deploy
# (zero-downtime: migrate first, then roll instances).
#
# Container:
#   docker compose -f docker-compose.prod.yml run --rm server --migrate
# Native:
#   ./scripts/db/migrate.sh            # runs the local binary with --migrate
#
# Migrations are idempotent (tracked in schema_migrations); re-running is safe.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BINARY="${RTC_BINARY:-${ROOT_DIR}/build/bin/realtime-chat}"

if [[ ! -x "${BINARY}" ]]; then
    echo "!! Binary not found at ${BINARY}. Build it, or set RTC_BINARY." >&2
    exit 1
fi

export MIGRATIONS_DIR="${MIGRATIONS_DIR:-${ROOT_DIR}/migrations}"
echo ">> Applying migrations via ${BINARY} --migrate"
exec "${BINARY}" --migrate
