#!/usr/bin/env bash
# Routine database maintenance: analyse/vacuum, reindex, and a quick health
# check. Safe to run on a live database (uses non-blocking variants).
#
#   DB_HOST=... DB_USER=chat DB_NAME=realtime_chat PGPASSWORD=... \
#     ./scripts/db/maintenance.sh [vacuum|reindex|health|all]
set -euo pipefail

DB_HOST="${DB_HOST:-localhost}"
DB_PORT="${DB_PORT:-5432}"
DB_USER="${DB_USER:-chat}"
DB_NAME="${DB_NAME:-realtime_chat}"
ACTION="${1:-all}"

psql_do() { psql -h "${DB_HOST}" -p "${DB_PORT}" -U "${DB_USER}" -d "${DB_NAME}" -v ON_ERROR_STOP=1 "$@"; }

vacuum() {
    echo ">> VACUUM (ANALYZE) — reclaim space and refresh planner statistics"
    psql_do -c "VACUUM (ANALYZE);"
}

reindex() {
    echo ">> REINDEX (CONCURRENTLY) hot indexes"
    for idx in ix_messages_conversation_id ix_messages_content_fts \
               ix_conversations_last_message_at ux_participants_conversation_user; do
        psql_do -c "REINDEX INDEX CONCURRENTLY ${idx};" || echo "   (skipped ${idx})"
    done
}

health() {
    echo ">> Connectivity"; psql_do -c "SELECT 1;" >/dev/null && echo "   OK"
    echo ">> Table sizes"
    psql_do -c "SELECT relname, pg_size_pretty(pg_total_relation_size(relid)) AS size
                FROM pg_catalog.pg_statio_user_tables ORDER BY pg_total_relation_size(relid) DESC LIMIT 10;"
    echo ">> Applied migrations"
    psql_do -c "SELECT version, applied_at FROM schema_migrations ORDER BY version;"
    echo ">> Connections"
    psql_do -c "SELECT count(*) AS connections FROM pg_stat_activity WHERE datname = current_database();"
}

case "${ACTION}" in
    vacuum)  vacuum ;;
    reindex) reindex ;;
    health)  health ;;
    all)     vacuum; reindex; health ;;
    *) echo "Usage: $0 [vacuum|reindex|health|all]"; exit 1 ;;
esac
