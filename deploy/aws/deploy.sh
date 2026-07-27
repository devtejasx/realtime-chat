#!/usr/bin/env bash
# Deploy/update realtime-chat on a host (EC2 or any Docker host).
#
# Zero-downtime-ish flow: pull the target ref, build, run migrations, then
# recreate the app container. Nginx keeps serving during the brief app restart;
# combined with a load balancer across ≥2 instances, deploys are fully seamless.
#
#   ./deploy/aws/deploy.sh [git-ref]     (default: main)
set -euo pipefail

REF="${1:-main}"
APP_DIR="${APP_DIR:-/opt/realtime-chat}"
COMPOSE="docker compose -f docker-compose.prod.yml"

cd "${APP_DIR}"

echo ">> Recording current revision for rollback"
git rev-parse HEAD > .last_deployed_rev || true

echo ">> Fetching ${REF}"
git fetch --all --tags
git checkout "${REF}"
git pull --ff-only origin "${REF}" || true

echo ">> Building image"
${COMPOSE} build server

echo ">> Applying migrations"
${COMPOSE} run --rm server --migrate

echo ">> Recreating services"
${COMPOSE} up -d

echo ">> Verifying health"
"$(dirname "${BASH_SOURCE[0]}")/health-check.sh"

echo ">> Deploy of ${REF} complete."
