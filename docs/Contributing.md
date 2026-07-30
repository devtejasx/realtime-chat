# Contributing

Thanks for your interest in realtime-chat. This guide covers the local workflow,
standards, and how changes are reviewed and released.

## Prerequisites

- A C++20 compiler (GCC 12+/Clang 15+), CMake ≥ 3.24, Ninja (optional).
- PostgreSQL + OpenSSL dev headers (`libpq-dev`, `libssl-dev`). Note that
  `postgresql-server-dev-all` is *not* required — that package exists to build
  PostgreSQL server extensions, and on Ubuntu 24.04 runners it conflicts with the
  PGDG `postgresql-common`. This project is a libpq client.
- `clang-format` and `clang-tidy` for local linting.

## Getting started

```bash
git clone https://github.com/devtejasx/realtime-chat.git
cd realtime-chat
./scripts/build.sh          # configure + build
./scripts/test.sh           # build + run the test suite
```

For a full local environment (Postgres + Redis + app):

```bash
docker compose -f docker-compose.dev.yml up --build
```

## Architecture rules (please follow)

- **Business logic lives only in services.** Controllers/WebSocket handlers are
  thin adapters; repositories contain only SQL.
- **Depend on interfaces**, not concretes; wire implementations in the single
  composition root (`Application`).
- **REST and WebSocket call the same services** — never duplicate logic.
- Prefer RAII, smart pointers, `std::optional`, `std::string_view`, move
  semantics, `constexpr`, and `enum class`. Avoid raw `new`/`delete`, global
  mutable state, and duplicated code.

See [Architecture.md](Architecture.md).

## Coding standards

- Formatting is enforced by `.clang-format` (Google base, C++20, 100 cols). Run
  `./scripts/format.sh` before committing.
- `.clang-tidy` runs in CI (advisory). Address findings where reasonable.
- Match the surrounding style, naming, and comment density.

## Tests

- Add/extend GoogleTest coverage for any behaviour change. Unit tests use
  in-memory fakes (no DB); database-backed tests are gated behind
  `RTC_RUN_DB_TESTS=1`.
- Do not remove or weaken existing tests. `ctest` must pass.

## Commits & pull requests

- Use clear, conventional commit messages (`feat(scope): …`, `fix(scope): …`,
  `docs: …`, `build: …`, `ci: …`, `test: …`).
- Keep PRs focused and backward-compatible. Explain **why** a change is needed.
- CI must be green: build & tests, lint, CodeQL, and secret scanning.

## CI checks

Every PR runs: build + tests (with a Postgres service), coverage, clang-format,
clang-tidy, CodeQL, gitleaks, and dependency review. See `.github/workflows/`.

## Reporting security issues

Please **do not** open a public issue for security vulnerabilities. Report them
privately to the maintainer so a fix can be prepared before disclosure. See
[Security.md](Security.md).

## License

By contributing you agree that your contributions are licensed under the
project's [MIT License](../LICENSE).
