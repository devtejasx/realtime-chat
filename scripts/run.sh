#!/usr/bin/env bash
# Run the server locally, loading environment variables from a .env file if
# present. Assumes the project has already been built (see scripts/build.sh) and
# that PostgreSQL is reachable (see docker-compose.yml for a local instance).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="${ROOT_DIR}/build/bin/realtime-chat"

if [[ -f "${ROOT_DIR}/.env" ]]; then
    echo ">> Loading environment from .env"
    set -a
    # shellcheck disable=SC1091
    source "${ROOT_DIR}/.env"
    set +a
fi

if [[ ! -x "${BINARY}" ]]; then
    echo "!! Server binary not found at ${BINARY}. Run scripts/build.sh first." >&2
    exit 1
fi

export MIGRATIONS_DIR="${MIGRATIONS_DIR:-${ROOT_DIR}/migrations}"
echo ">> Starting realtime-chat on port ${CHAT_PORT:-8080}"
exec "${BINARY}"
