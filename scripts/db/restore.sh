#!/usr/bin/env bash
# Restore the database from a pg_dump custom-format backup.
#
#   DB_HOST=... DB_USER=chat DB_NAME=realtime_chat PGPASSWORD=... \
#     ./scripts/db/restore.sh backups/realtime_chat-<stamp>.dump
#
# WARNING: this replaces objects in the target database. Verifies the checksum
# first when a .sha256 sidecar is present.
set -euo pipefail

FILE="${1:?Usage: restore.sh <backup.dump>}"
DB_HOST="${DB_HOST:-localhost}"
DB_PORT="${DB_PORT:-5432}"
DB_USER="${DB_USER:-chat}"
DB_NAME="${DB_NAME:-realtime_chat}"

if [[ -f "${FILE}.sha256" ]]; then
    echo ">> Verifying checksum"
    sha256sum -c "${FILE}.sha256"
fi

echo ">> Restoring ${FILE} into ${DB_NAME} on ${DB_HOST}"
read -r -p "This will overwrite data in ${DB_NAME}. Continue? [y/N] " confirm
[[ "${confirm}" == "y" || "${confirm}" == "Y" ]] || { echo "Aborted"; exit 1; }

pg_restore -h "${DB_HOST}" -p "${DB_PORT}" -U "${DB_USER}" -d "${DB_NAME}" \
    --clean --if-exists --no-owner --no-privileges --jobs=4 "${FILE}"

echo ">> Restore complete."
