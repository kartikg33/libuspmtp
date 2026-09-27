# syntax=docker/dockerfile:1
#
# examples/rust/Dockerfile
#
# Builds libuspmtp as a SHARED library (libuspmtp.so) and the Rust example
# binary, which links against it dynamically, then runs it against an
# OB-USPA UNIX domain socket mounted from the host.
#
# The Rust example uses only the standard library plus a hand-written FFI
# layer (no external crates), so the Cargo build is fully offline.
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
#   docker build -t libuspmtp-example-rust -f examples/rust/Dockerfile .
#
#   # Run, mounting the host socket directory and passing the agent endpoint
#   docker run --rm \
#     -v /var/run/usp:/var/run/usp \
#     -e USP_SOCKET_PATH=/var/run/usp/broker_agent_path \
#     -e USP_AGENT_ENDPOINT_ID=proto::api-gateway \
#     libuspmtp-example-rust
#
# The socket directory on the host (e.g. /var/run/usp) must be bind-mounted
# into the container at the same path (or wherever USP_SOCKET_PATH points).

# ── Stage 1: build ─────────────────────────────────────────────────────────
FROM rust:1-bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ca-certificates \
        libprotobuf-dev \
        protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy the library (needed as a dependency of the example build) and the
# Rust example itself.
COPY CMakeLists.txt        ./
COPY cmake/                cmake/
COPY include/              include/
COPY proto/                proto/
COPY src/                  src/
COPY examples/rust/        examples/rust/

# Build and install the SHARED library (libuspmtp.so). The Rust example links
# against it dynamically (#[link(kind = "dylib")]), so the top-level project
# must be built shared here — unlike the C/C++ examples, which use the static
# archive.
RUN cmake -S . -B /libbuild \
        -DCMAKE_BUILD_TYPE=Release \
        -DLIBUSPMTP_BUILD_SHARED=ON \
        -DLIBUSPMTP_BUILD_TESTS=OFF \
    && cmake --build /libbuild --parallel "$(nproc)" \
    && cmake --install /libbuild --prefix /usr/local

# Build the Rust example as its own application against the installed shared
# library. Fully offline: the example has no external crates.
# The trailing ldd check fails the build unless the binary really links
# libuspmtp.so dynamically.
ENV LIBUSPMTP_LIB_DIR=/usr/local/lib
ENV CARGO_NET_OFFLINE=true
RUN cargo build --locked --release \
        --manifest-path examples/rust/Cargo.toml \
        --target-dir /build \
    && ldd /build/release/example_rust | grep libuspmtp

# ── Stage 2: runtime ───────────────────────────────────────────────────────
FROM debian:bookworm-slim

# The dynamically linked binary needs the C++ runtime, protobuf, and
# libuspmtp.so itself at runtime.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 \
        libprotobuf32 \
    && rm -rf /var/lib/apt/lists/*

# Default socket path; override with -e USP_SOCKET_PATH=...
ENV USP_SOCKET_PATH=/var/run/usp/broker_agent_path
ENV USP_APP_ENDPOINT_ID=proto::myapp
ENV USP_AGENT_ENDPOINT_ID=proto::api-gateway

COPY --from=builder /usr/local/lib/libuspmtp.so* /usr/local/lib/
RUN ldconfig

COPY --from=builder /build/release/example_rust /usr/local/bin/example_rust

# The socket directory must be bind-mounted from the host at runtime.
# Declare it as a volume so consumers know to mount it.
VOLUME ["/var/run/usp"]

ENTRYPOINT ["/usr/local/bin/example_rust"]
