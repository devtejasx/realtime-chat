#!/usr/bin/env bash
# Verify the service is live and ready, retrying until it passes or times out.
# Used by deploy/rollback scripts and suitable as an ALB/target-group probe.
#
#   HEALTH_URL=http://localhost:8080/health/ready ./deploy/aws/health-check.sh
set -euo pipefail

HEALTH_URL="${HEALTH_URL:-http://localhost:8080/health/ready}"
LIVE_URL="${LIVE_URL:-http://localhost:8080/health/live}"
RETRIES="${RETRIES:-30}"
SLEEP="${SLEEP:-2}"

echo ">> Checking liveness at ${LIVE_URL}"
for i in $(seq 1 "${RETRIES}"); do
    if curl -fsS "${LIVE_URL}" >/dev/null 2>&1; then
        echo "   alive"
        break
    fi
    [[ "${i}" == "${RETRIES}" ]] && { echo "!! Liveness failed" >&2; exit 1; }
    sleep "${SLEEP}"
done

echo ">> Checking readiness at ${HEALTH_URL}"
for i in $(seq 1 "${RETRIES}"); do
    code="$(curl -s -o /dev/null -w '%{http_code}' "${HEALTH_URL}" || echo 000)"
    if [[ "${code}" == "200" ]]; then
        echo "   ready"
        exit 0
    fi
    echo "   not ready (HTTP ${code}), attempt ${i}/${RETRIES}"
    sleep "${SLEEP}"
done

echo "!! Service did not become ready in time" >&2
exit 1
