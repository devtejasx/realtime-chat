#!/usr/bin/env bash
# Obtain the initial Let's Encrypt certificate for the Nginx reverse proxy.
#
# Usage:
#   DOMAIN=chat.example.com EMAIL=ops@example.com ./scripts/ssl/init-letsencrypt.sh
#
# Renewal afterwards is automatic via the `certbot` service in
# docker-compose.prod.yml (it runs `certbot renew` on a schedule). This script
# only bootstraps the first certificate using the webroot challenge.
set -euo pipefail

DOMAIN="${DOMAIN:?Set DOMAIN=your.domain}"
EMAIL="${EMAIL:?Set EMAIL=you@example.com}"
STAGING="${STAGING:-0}"   # set STAGING=1 to use Let's Encrypt staging (no rate limit)
COMPOSE="docker compose -f docker-compose.prod.yml"

echo ">> Bootstrapping certificate for ${DOMAIN}"

# 1) Start Nginx so it can serve the ACME challenge over HTTP.
${COMPOSE} up -d nginx

# 2) Request the certificate via the webroot challenge.
staging_arg=""
if [[ "${STAGING}" == "1" ]]; then
    staging_arg="--staging"
fi

${COMPOSE} run --rm --entrypoint "\
  certbot certonly --webroot -w /var/www/certbot \
    ${staging_arg} \
    --email ${EMAIL} \
    -d ${DOMAIN} \
    --rsa-key-size 4096 \
    --agree-tos \
    --no-eff-email \
    --non-interactive" certbot

# 3) Reload Nginx to pick up the new certificate.
echo ">> Reloading Nginx"
${COMPOSE} exec nginx nginx -s reload

echo ">> Done. Certificate installed for ${DOMAIN}."
echo ">> Remember to set server_name and certificate paths in nginx/conf.d/realtime-chat.conf."
