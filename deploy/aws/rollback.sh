#!/usr/bin/env bash
# Roll back to the previously deployed revision (recorded by deploy.sh) or an
# explicit git ref. Rebuilds and recreates the app container.
#
#   ./deploy/aws/rollback.sh [git-ref]
#
# NOTE: if the rolled-back revision has an *older* schema than what is deployed,
# restore the pre-migration database backup first (see scripts/db/rollback.sh).
set -euo pipefail

APP_DIR="${APP_DIR:-/opt/realtime-chat}"
COMPOSE="docker compose -f docker-compose.prod.yml"
cd "${APP_DIR}"

REF="${1:-}"
if [[ -z "${REF}" ]]; then
    if [[ -f .last_deployed_rev ]]; then
        REF="$(cat .last_deployed_rev)"
    else
        echo "!! No .last_deployed_rev found and no ref given" >&2
        exit 1
    fi
fi

echo ">> Rolling back to ${REF}"
git checkout "${REF}"
${COMPOSE} build server
${COMPOSE} up -d server
"$(dirname "${BASH_SOURCE[0]}")/health-check.sh"
echo ">> Rollback to ${REF} complete."
