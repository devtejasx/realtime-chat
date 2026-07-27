#!/usr/bin/env bash
# EC2 user-data: bootstrap a fresh Ubuntu 24.04 instance to run realtime-chat.
#
# Paste into the EC2 launch "User data" field (or an ASG launch template). It
# installs Docker, clones the repo, and starts the production stack. Application
# secrets should come from SSM Parameter Store / Secrets Manager rather than
# being baked in — fetch them into /opt/realtime-chat/.env before `up`.
set -euxo pipefail

REPO_URL="${REPO_URL:-https://github.com/devtejasx/realtime-chat.git}"
APP_DIR="/opt/realtime-chat"

# --- Docker ---
apt-get update
apt-get install -y ca-certificates curl git
install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
chmod a+r /etc/apt/keyrings/docker.asc
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] \
  https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo "$VERSION_CODENAME") stable" \
  > /etc/apt/sources.list.d/docker.list
apt-get update
apt-get install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin
systemctl enable --now docker

# --- App ---
git clone "${REPO_URL}" "${APP_DIR}" || (cd "${APP_DIR}" && git pull)
cd "${APP_DIR}"

# Fetch secrets (example — replace with your SSM/Secrets Manager retrieval):
#   aws ssm get-parameter --with-decryption --name /realtime-chat/env --query Parameter.Value --output text > .env
if [[ ! -f .env ]]; then
    echo "WARNING: /opt/realtime-chat/.env not found; provide JWT_SECRET/DB_PASSWORD before start" >&2
fi

# Apply migrations, then start the stack.
docker compose -f docker-compose.prod.yml run --rm server --migrate || true
docker compose -f docker-compose.prod.yml up -d --build
