# syntax=docker/dockerfile:1
#
# examples/c/Dockerfile
#
# Builds libuspmtp (static) and the C example binary, then runs it against
# an OB-USPA UNIX domain socket mounted from the host.
#
# Environment variables (configure before running):
#   USP_SOCKET_PATH       Path to the OB-USPA UDS socket inside the container.
#                         Default: /var/run/usp/broker_agent_path
#   USP_APP_ENDPOINT_ID   This application's USP endpoint ID (from_id).
#                         Default: proto::myapp
#   USP_AGENT_ENDPOINT_ID The USP agent's endpoint ID (to_id / api-gateway).
#                         Default: proto::api-gateway
#
# Typical usage:
#   # Build the image
#   docker build -t libuspmtp-example-c -f examples/c/Dockerfile .
#
#   # Run, mounting the host socket directory and passing the agent endpoint
#   docker run --rm \
#     -v /var/run/usp:/var/run/usp \
#     -e USP_SOCKET_PATH=/var/run/usp/broker_agent_path \
#     -e USP_AGENT_ENDPOINT_ID=proto::api-gateway \
#     libuspmtp-example-c
#
# The socket directory on the host (e.g. /var/run/usp) must be bind-mounted
# into the container at the same path (or wherever USP_SOCKET_PATH points).

# ── Stage 1: build ─────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ca-certificates \
        libprotobuf-dev \
        protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy the library (needed as a dependency of the example build) and the
# C example itself.
COPY CMakeLists.txt        ./
COPY cmake/                cmake/
COPY include/              include/
COPY proto/                proto/
COPY src/                  src/
COPY examples/c/           examples/c/

# Build the C example as its own application. The example's CMakeLists pulls
# the library in via add_subdirectory and links the binary against it
# (statically), so the top-level project only builds the library.
RUN cmake -S examples/c -B /build \
        -DCMAKE_BUILD_TYPE=Release \
        -DLIBUSPMTP_SOURCE_DIR=/src \
        -DLIBUSPMTP_BUILD_TESTS=OFF \
    && cmake --build /build --parallel "$(nproc)"

# ── Stage 2: runtime ───────────────────────────────────────────────────────
FROM debian:bookworm-slim

# Default socket path; override with -e USP_SOCKET_PATH=...
ENV USP_SOCKET_PATH=/var/run/usp/broker_agent_path
ENV USP_APP_ENDPOINT_ID=proto::myapp
ENV USP_AGENT_ENDPOINT_ID=proto::api-gateway

COPY --from=builder /build/example_c /usr/local/bin/example_c

# The socket directory must be bind-mounted from the host at runtime.
# Declare it as a volume so consumers know to mount it.
VOLUME ["/var/run/usp"]

ENTRYPOINT ["/usr/local/bin/example_c"]
