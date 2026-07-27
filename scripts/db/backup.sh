#!/usr/bin/env bash
# Create a compressed, timestamped logical backup of the database.
#
#   DB_HOST=... DB_USER=chat DB_NAME=realtime_chat PGPASSWORD=... \
#     ./scripts/db/backup.sh [output_dir]
#
# Uses the custom pg_dump format (-Fc) so restores can be parallelised and
# selective. Verifies the dump is non-empty before reporting success.
set -euo pipefail

DB_HOST="${DB_HOST:-localhost}"
DB_PORT="${DB_PORT:-5432}"
DB_USER="${DB_USER:-chat}"
DB_NAME="${DB_NAME:-realtime_chat}"
OUT_DIR="${1:-backups}"

mkdir -p "${OUT_DIR}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
FILE="${OUT_DIR}/${DB_NAME}-${STAMP}.dump"

echo ">> Backing up ${DB_NAME} to ${FILE}"
pg_dump -h "${DB_HOST}" -p "${DB_PORT}" -U "${DB_USER}" -d "${DB_NAME}" \
    --format=custom --no-owner --no-privileges --file "${FILE}"

if [[ ! -s "${FILE}" ]]; then
    echo "!! Backup file is empty — aborting" >&2
    exit 1
fi

sha256sum "${FILE}" > "${FILE}.sha256"
echo ">> Backup complete: $(du -h "${FILE}" | cut -f1)"
echo ">> Checksum: ${FILE}.sha256"

# Optional retention: delete backups older than RETENTION_DAYS (default 14).
RETENTION_DAYS="${RETENTION_DAYS:-14}"
find "${OUT_DIR}" -name "${DB_NAME}-*.dump" -mtime "+${RETENTION_DAYS}" -delete || true
