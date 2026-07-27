#!/usr/bin/env bash
# Roll back the most recent migration by restoring from a pre-migration backup.
#
# This project uses forward-only migrations (no down-scripts), which is the
# safest strategy for a production dataset: the rollback path is "restore the
# backup taken immediately before the migration ran". Always back up before
# migrating (see scripts/db/backup.sh).
#
#   ./scripts/db/rollback.sh backups/realtime_chat-<pre-migration-stamp>.dump
set -euo pipefail

FILE="${1:?Usage: rollback.sh <pre-migration-backup.dump>}"
echo ">> Rolling back by restoring the pre-migration backup: ${FILE}"
echo ">> (Forward-only migrations: recovery is restore-based.)"
exec "$(dirname "${BASH_SOURCE[0]}")/restore.sh" "${FILE}"
