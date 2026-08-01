# Release process

realtime-chat uses **semantic versioning** (`vMAJOR.MINOR.PATCH`) and automated
release pipelines.

## Versioning

- **MAJOR** — incompatible API/schema changes.
- **MINOR** — backward-compatible features.
- **PATCH** — backward-compatible fixes.

The project version is set in the top-level `CMakeLists.txt` (`project(... VERSION
x.y.z)`) and surfaced at runtime (`/health`, logs) via the `RTC_VERSION` define.

## Cutting a release

1. Ensure `main` is green (build, tests, lint, CodeQL, security).
2. Bump `VERSION` in `CMakeLists.txt`; update any docs referencing the version.
3. Commit: `git commit -m "chore(release): vX.Y.Z"`.
4. Tag and push:

   ```bash
   git tag -a vX.Y.Z -m "vX.Y.Z"
   git push origin main --follow-tags
   ```

## What automation does on a tag

Pushing a `v*` tag triggers:

- **`release.yml`** — builds a Release binary, packages
  `realtime-chat-vX.Y.Z-linux-x86_64.tar.gz` (binary + migrations + README +
  LICENSE + `.env.example`) with a SHA-256 checksum, and publishes a **GitHub
  Release** with auto-generated notes and the artifacts attached.
- **`docker.yml`** — builds the production image and pushes it to
  `ghcr.io/devtejasx/realtime-chat-server` tagged with the semver
  (`X.Y.Z`, `X.Y`) and the commit SHA.

## Deploying a release

Container hosts:

```bash
docker pull ghcr.io/devtejasx/realtime-chat-server:X.Y.Z
# update the image tag in your compose/orchestrator, run migrations, roll:
docker compose -f docker-compose.prod.yml run --rm server --migrate
docker compose -f docker-compose.prod.yml up -d
```

Or use the scripts:

```bash
./deploy/aws/deploy.sh vX.Y.Z
```

## Rollback

```bash
./deploy/aws/rollback.sh vX.Y-1.Z      # previous tag
```

If the previous version expects an older schema, restore the pre-migration
database backup first (`scripts/db/rollback.sh`). Migrations are forward-only.

## Changelog

Release notes are generated automatically from merged PRs/commits by
`release.yml` (`generate_release_notes: true`). Write descriptive PR titles so
the changelog is meaningful.

## Post-release checklist

- [ ] GitHub Release published with artifacts + checksum.
- [ ] Container image available in GHCR at the new tag.
- [ ] Production deployed and `/health/ready` returns 200.
- [ ] Metrics/alerts nominal (see [Monitoring.md](Monitoring.md)).
- [ ] Backups verified for the new schema version.
