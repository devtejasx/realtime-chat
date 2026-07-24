# syntax=docker/dockerfile:1

# ============================================================================
# Stage 1: build
# Compiles the server and its dependencies. Dependencies are resolved by CMake
# (FetchContent) at configure time, so the builder needs git, a toolchain, and
# the PostgreSQL/OpenSSL development headers.
# ============================================================================
FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        pkg-config \
        libssl-dev \
        libpq-dev \
        postgresql-server-dev-all \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy the build definition first so dependency configuration is cached across
# source-only changes.
COPY CMakeLists.txt ./
COPY cmake ./cmake

# Now copy the remaining sources and build.
COPY include ./include
COPY src ./src
COPY tests ./tests
COPY migrations ./migrations

RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DRTC_BUILD_TESTS=OFF \
    && cmake --build build --parallel

# ============================================================================
# Stage 2: runtime
# Minimal image carrying only the binary, its shared-library dependencies, and
# the migration scripts.
# ============================================================================
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3 \
        libpq5 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home --uid 10001 appuser

WORKDIR /app

COPY --from=build /src/build/bin/realtime-chat /app/realtime-chat
COPY --from=build /src/migrations /app/migrations

ENV CHAT_PORT=8080 \
    MIGRATIONS_DIR=/app/migrations

EXPOSE 8080

USER appuser

ENTRYPOINT ["/app/realtime-chat"]
