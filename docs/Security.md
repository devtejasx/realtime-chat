# Security

realtime-chat follows OWASP guidance across authentication, transport, input
handling, and operations.

## Authentication & sessions

- **Passwords** hashed with **bcrypt** (tunable cost); plaintext never stored or
  logged. Length capped at 72 bytes (bcrypt's limit) rather than silently
  truncated.
- **JWT** access/refresh tokens (HS256) with issuer + expiry; verification
  enforces signature, issuer, expiry, and token *type* (an access token can't be
  used as a refresh token).
- **Distributed sessions**: refresh tokens are stored only as SHA-256 hashes and
  **rotated on every refresh** — a previously-used refresh token is rejected
  (replay protection). Sessions can be revoked individually or globally (logout
  all devices).
- **Rate limiting** on login, registration, messaging, and uploads (per user /
  IP) returns `429` — brute-force mitigation.

## Transport

- TLS terminated at Nginx: **TLS 1.2+**, modern cipher suites, OCSP stapling.
- **HSTS** (2 years, `includeSubDomains; preload`).
- HTTP is redirected to HTTPS (except the ACME challenge path).
- Certificates via Let's Encrypt with automatic renewal.

## HTTP hardening

Security headers are applied by both the app (`SecurityMiddleware`) and Nginx
(defence in depth), sent even on error responses:

- `X-Content-Type-Options: nosniff`
- `X-Frame-Options: DENY` and CSP `frame-ancestors 'none'`
- `Content-Security-Policy: default-src 'none'` (this is a JSON API; no inline
  scripts/resources are served)
- `Referrer-Policy: no-referrer`
- `Permissions-Policy` disabling geolocation/mic/camera (Nginx)
- **CORS** is explicit and configurable (`CORS_ALLOWED_ORIGINS`); do not use `*`
  in production.

### CSRF

The API is **token-authenticated via the `Authorization` header**, not cookies,
so it is not susceptible to classic CSRF (a browser won't attach a bearer token
cross-site). If you introduce cookie auth, add CSRF tokens and `SameSite`
cookies.

## Upload security

- Explicit **MIME allow-list**; everything else is rejected with `415`.
- Declared content type is **cross-checked against the file extension**.
- Per-upload **size limit** (`MAX_UPLOAD_BYTES`) enforced in the app and mirrored
  by Nginx `client_max_body_size` → `413`.
- Storage keys are **traversal-safe** (no absolute paths, no `..`), generated
  server-side; raw keys are never exposed to clients.
- SHA-256 checksum recorded per attachment.

## Input validation & error handling

- All inbound JSON is structurally validated at the DTO boundary; semantic rules
  (lengths, formats) live in the validation layer.
- Errors return a consistent envelope; internal exception text is **never leaked**
  to clients (masked `500`), only logged.
- SQL uses **parameterised statements** everywhere (no string concatenation of
  user input).

## Secrets & configuration

- `JWT_SECRET` must be strong and unique per environment; in `production` the app
  **refuses to start** with the shipped default or a secret shorter than 32
  bytes.
- Provide secrets via environment / secrets manager (SSM, Secrets Manager),
  never committed. `.env` is git-ignored.
- Connection strings are **redacted** in logs.

## Supply chain

- Dependencies are pinned to explicit tags in `cmake/dependencies.cmake`.
- CI runs **CodeQL**, **gitleaks** (secret detection), and **dependency review**.
- The runtime container runs **non-root**.

## Operational

- Graceful shutdown drains connections and background workers on `SIGTERM`.
- The `backend` Docker network is `internal` — the database and cache are not
  reachable from outside the host.
- systemd unit adds `NoNewPrivileges`, `ProtectSystem=strict`, `PrivateTmp`, etc.

## Reporting a vulnerability

See [Contributing.md](Contributing.md). Please report security issues privately
rather than opening a public issue.
