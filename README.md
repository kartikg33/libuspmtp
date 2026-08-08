<div align="center">

# libuspmtp

### A modern C++20 USP (TR-369) MTP protocol library

**Talk to USP Agents through a safe, stable, C-compatible API.**

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](#)
[![C API](https://img.shields.io/badge/API-C%20ABI-informational.svg)](#)
[![License](https://img.shields.io/badge/license-PLACEHOLDER-lightgrey.svg)](#)
[![Build](https://img.shields.io/badge/build-PLACEHOLDER-lightgrey.svg)](#)
[![Tests](https://img.shields.io/badge/tests-PLACEHOLDER-lightgrey.svg)](#)
[![Coverage](https://img.shields.io/badge/coverage-PLACEHOLDER-lightgrey.svg)](#)
[![Release](https://img.shields.io/badge/release-PLACEHOLDER-lightgrey.svg)](#)

<br />

**C++20 implementation · C ABI · Static & Shared Libraries · USP / TR-369**

<br />

[Features](#-features) ·
[API](#-api-status) ·
[Quick Start](#-quick-start) ·
[Build](#-build) ·
[Examples](#-examples) ·
[Architecture](#-architecture) ·
[Contributing](#-contributing)

</div>

---

## ✨ Overview

**libuspmtp** is a modern C++20 library for communicating with **USP Agents** implementing the Broadband Forum **USP (TR-369)** protocol.

It provides a clean controller API for operations such as:

- `GET`
- `GET_SUPPORTED_DM`
- `GET_INSTANCES`
- `SET`
- `ADD`
- `DELETE`
- `OPERATE`
- `REGISTER`
- `GET_SUPPORTED_PROTOCOL`
- `SUBSCRIBE`
- `SUBSCRIBE_AND_GET`

The library is designed around a simple principle:

> **Modern C++20 internally. Stable C ABI externally.**

The implementation uses modern C++ ownership, RAII, concurrency primitives, and asynchronous facilities while exposing a conservative C-compatible API that can be consumed by C, modern C++, and legacy C/C++ applications.

---

## 🚀 Why libuspmtp?

USP controllers frequently need to live inside long-running embedded systems, network services, management applications, and existing C/C++ codebases.

libuspmtp is designed for those environments.

### Modern C++20

Built using modern C++20 facilities with a focus on:

- RAII
- Explicit ownership
- Strong lifetime guarantees
- Type safety
- Structured concurrency
- Safe asynchronous operations
- C++20 coroutines where appropriate

### Stable C ABI

The public API is exposed through a C-compatible header.

That means the same library can be consumed by:

```text
C
C++20
Legacy C++
Other languages with C FFI support
```

without exposing C++ implementation details.

### Static or shared

Build and distribute the library as either:

```text
Static library
Shared library
```

depending on the needs of your application.

### Designed for embedded and systems software

The library is designed with long-running processes, constrained systems, asynchronous events, deterministic cleanup, and predictable resource ownership in mind.

---

## 📦 Features

| Feature | Status |
|---|:---:|
| USP GET | ✅ |
| USP GET_MANY | ✅ |
| USP SET | ✅ |
| USP SET_MANY | ✅ |
| USP OPERATE | ✅ |
| USP SUBSCRIBE_AND_GET | ✅ |
| USP SUBSCRIBE_MANY_AND_GET | ✅ |
| USP REGISTER | 🚧 |
| USP ADD | 🚧 |
| USP DELETE | 🚧 |
| USP GET_SUPPORTED_DM | 🚧 |
| USP GET_INSTANCES | 🚧 |
| USP GET_SUPPORTED_PROTOCOL | 🚧 |
| C-compatible public API | ✅ |
| C++20 implementation | ✅ |
| Static library | ✅ |
| Shared library | ✅ |
| C examples | 🚧 |
| C++ examples | 🚧 |
| Sanitizer builds | 🚧 |
| Package-manager distribution | 🚧 |

> **Legend:** ✅ Implemented · 🚧 Planned / In progress · ❌ Not supported

---

# 📚 API Status

| API | Status | Description |
|---|:---:|---|
| `usp_controller_new()` | ✅ | Create a controller |
| `usp_controller_set_timeout()` | ✅ | Configure operation timeout |
| `usp_controller_get()` | ✅ | Retrieve parameters |
| `usp_controller_get_many()` | ✅ | Retrieve multiple paths |
| `usp_controller_set()` | ✅ | Update a parameter |
| `usp_controller_set_many()` | ✅ | Update multiple parameters |
| `usp_controller_operate()` | ✅ | Execute a USP command |
| `usp_controller_subscribe_and_get()` | ✅ | Subscribe and retrieve initial state |
| `usp_controller_subscribe_many_and_get()` | ✅ | Create multiple subscriptions and retrieve state |
| `usp_controller_register()` | 🚧 | Register controller |
| `usp_controller_add()` | 🚧 | Create object instance |
| `usp_controller_delete()` | 🚧 | Delete object instance |
| `usp_controller_get_supported_dm()` | 🚧 | Query supported data model |
| `usp_controller_get_instances()` | 🚧 | Query object instances |
| `usp_controller_get_supported_protocol()` | 🚧 | Query supported USP protocol |

APIs marked as planned currently return an appropriate `NOT_IMPLEMENTED` status.

---

# ⚡ Quick Start

## C

The public API is intentionally C-compatible.

```c
#include "libuspmtp.h"
#include <stdio.h>

int main(void)
{
    UspControllerHandle* controller =
        usp_controller_new(
            "/var/run/usp/broker_agent_path",
            "proto::my-app",
            "proto::api-gateway",
            10
        );

    if (controller == NULL) {
        return 1;
    }

    char value[256];

    const int32_t rc =
        usp_controller_get(
            controller,
            "Device.DeviceInfo.SerialNumber",
            value,
            sizeof(value)
        );

    if (rc == USP_OK) {
        printf("SerialNumber=%s\n", value);
    } else {
        char error[256];

        usp_controller_last_error(
            controller,
            error,
            sizeof(error)
        );

        fprintf(
            stderr,
            "USP error (%d): %s\n",
            rc,
            error
        );
    }

    usp_controller_free(controller);

    return 0;
}
```

---

## C++20

The same C API can be consumed naturally from C++20.

```cpp
#include "libuspmtp.h"

#include <array>
#include <iostream>
#include <memory>

int main()
{
    using ControllerPtr =
        std::unique_ptr<
            UspControllerHandle,
            decltype(&usp_controller_free)
        >;

    ControllerPtr controller{
        usp_controller_new(
            "/var/run/usp/broker_agent_path",
            "proto::my-cpp-app",
            "proto::api-gateway",
            10
        ),
        &usp_controller_free
    };

    if (!controller) {
        return 1;
    }

    std::array<char, 256> value{};

    const auto rc =
        usp_controller_get(
            controller.get(),
            "Device.DeviceInfo.SerialNumber",
            value.data(),
            value.size()
        );

    if (rc == USP_OK) {
        std::cout
            << "SerialNumber="
            << value.data()
            << '\n';

        return 0;
    }

    std::array<char, 256> error{};

    usp_controller_last_error(
        controller.get(),
        error.data(),
        error.size()
    );

    std::cerr
        << "USP error ("
        << rc
        << "): "
        << error.data()
        << '\n';

    return 1;
}
```

---

# 🛠️ Build

## Requirements

### Required

- C++20 compiler
- CMake
- A supported platform/compiler combination

### Supported compilers

| Compiler | Version |
|---|---|
| GCC | **PLACEHOLDER** |
| Clang | **PLACEHOLDER** |
| MSVC | **PLACEHOLDER** |

---

## Configure

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release
```

## Build

```bash
cmake --build build --parallel
```

## Run tests

```bash
ctest --test-dir build --output-on-failure
```

---

# 📦 Static Library

Configure a static build:

```bash
cmake -S . -B build-static \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF
```

Build:

```bash
cmake --build build-static --parallel
```

The resulting library will be available under the project's configured library output directory.

---

# 🔗 Shared Library

Configure a shared build:

```bash
cmake -S . -B build-shared \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON
```

Build:

```bash
cmake --build build-shared --parallel
```

---

# 🧩 CMake Integration

Once installed, applications should be able to consume the library using a normal CMake package:

```cmake
find_package(libuspmtp CONFIG REQUIRED)

target_link_libraries(
    my_application
    PRIVATE
        libuspmtp::libuspmtp
)
```

> **TODO:** Replace the target/package names above with the final exported CMake target.

---

# 📁 Project Layout

```text
libuspmtp/
├── AGENTS.md
├── README.md
├── LICENSE
├── CMakeLists.txt
│
├── include/
│   └── libuspmtp.h
│
├── src/
│   ├── ...
│   └── internal/
│
├── tests/
│   ├── c/
│   ├── cpp/
│   └── ...
│
├── examples/
│   ├── c/
│   └── cpp/
│
├── cmake/
│   └── ...
│
└── docs/
    └── ...
```

The `include/` directory contains the public API.

Internal C++ implementation details must not leak into the public C header.

---

# 🏗️ Architecture

libuspmtp is deliberately split into two layers.

```text
┌─────────────────────────────────────────────────────────┐
│                    Application                          │
│                                                         │
│       C application / C++ application / FFI            │
└───────────────────────────┬─────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│                  Public C API                           │
│                                                         │
│              libuspmtp.h                         │
└───────────────────────────┬─────────────────────────────┘
                            │
                     Stable C ABI
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│                  C++20 Core                             │
│                                                         │
│   RAII · Ownership · Concurrency · Coroutines           │
│   Protocol · Transport · Callbacks · Error Handling     │
└───────────────────────────┬─────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│              Message Transfer Transport                 │
│                                                         │
│                    UDS / ...                             │
└───────────────────────────┬─────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│                    USP Agent                            │
│                                                         │
│                  TR-369 / USP                           │
└─────────────────────────────────────────────────────────┘
```

The public ABI intentionally exposes opaque handles and C-compatible data structures.

Internally, the implementation is free to use modern C++20 abstractions without coupling consumers to C++ ABI details.

---

# 🔒 Memory & Thread Safety

Memory safety and deterministic ownership are core design requirements.

The implementation follows modern C++ practices including:

- RAII
- `std::unique_ptr`
- `std::shared_ptr` where genuinely required
- Explicit ownership
- Explicit lifetime management
- `std::jthread`
- `std::stop_token`
- C++20 synchronization primitives
- C++20 coroutines where appropriate
- Sanitizer-friendly code

C++ exceptions never cross the public C ABI.

Asynchronous operations must have explicit ownership, cancellation, and shutdown semantics.

For the complete engineering standard, see [`AGENTS.md`](AGENTS.md).

---

# 🌐 Transport

The initial transport implementation uses **Unix Domain Sockets (UDS)**.

Typical deployments may communicate with a USP Agent through a local broker or USP transport endpoint.

Example:

```text
libuspmtp
       │
       │ UDS
       ▼
USP Message Broker
       │
       ▼
USP Agent
```

Additional transports may be added in the future without changing the core controller API.

---

# 📖 Usage

## Controller creation

```c
UspControllerHandle* controller =
    usp_controller_new(
        "/var/run/usp/broker_agent_path",
        "proto::my-app",
        "proto::api-gateway",
        10
    );
```

The controller configuration includes:

| Parameter | Description |
|---|---|
| `socket_path` | USP transport socket |
| `app_endpoint_id` | Controller endpoint identifier |
| `agent_endpoint_id` | Target USP Agent endpoint identifier |
| `timeout` | Request timeout |

---

## GET

Retrieve parameters from the Agent:

```c
usp_controller_get(
    controller,
    "Device.DeviceInfo.SerialNumber",
    buffer,
    sizeof(buffer)
);
```

---

## SET

Update a parameter:

```c
usp_controller_set(
    controller,
    "Device.WiFi.SSID.1.SSID",
    "MyNetwork"
);
```

---

## OPERATE

Execute an operation:

```text
Device.IP.Diagnostics.IPPing()
```

with operation arguments such as:

```text
Host = 8.8.8.8
```

---

## SUBSCRIBE

Register for USP notification events.

The library supports subscription workflows including:

```text
SUBSCRIBE_AND_GET
SUBSCRIBE_MANY_AND_GET
```

with notification types such as:

```text
Value Change
Object Creation
Object Deletion
```

---

# 🧪 Testing

Run the complete test suite with:

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON

cmake --build build --parallel

ctest \
    --test-dir build \
    --output-on-failure
```

The test suite covers:

- Public API behavior
- Error handling
- Ownership
- Object lifetime
- Transport behavior
- USP operations
- Callbacks
- Asynchronous operations
- Concurrency
- Shutdown
- C ABI compatibility
- C++ integration

---

# 🧰 Sanitizers

Sanitizer builds are strongly recommended during development.

Example configuration:

```bash
cmake -S . -B build-asan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DLIBUSPMTP_ENABLE_ASAN=ON \
    -DLIBUSPMTP_ENABLE_UBSAN=ON
```

Then:

```bash
cmake --build build-asan --parallel

ctest \
    --test-dir build-asan \
    --output-on-failure
```

ThreadSanitizer should be used where supported:

```bash
cmake -S . -B build-tsan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DLIBUSPMTP_ENABLE_TSAN=ON
```

> **TODO:** Replace the option names above with the final CMake configuration.

---

# 📊 USP Error Codes

The library exposes USP error information through its public error API.

| Code | Name | Applicability | Description |
|---:|---|---|---|
| 7000 | Message failed | Error Message | General failure described by the associated error message. |
| 7001 | Message not supported | Error Message | The target endpoint did not understand the message. |
| 7002 | Request denied | Error Message | The target endpoint cannot or will not process the request. |
| 7003 | Internal error | Error Message | Internal hardware or software failure. |
| 7004 | Invalid arguments | Error Message | Invalid values were supplied in the USP message. |
| 7005 | Resources exceeded | Error Message | Memory or processing resources were insufficient. |
| 7006 | Permission denied | Error Message | The source endpoint is not authorized to perform the action. |
| 7007 | Invalid configuration | Error Message | Processing the request would result in an invalid configuration. |
| 7008 | Invalid path syntax | `requested_path` | The supplied path syntax was not understood. |
| 7009 | Parameter action failed | SET | A parameter update failed. |
| 7010 | Unsupported parameter | ADD, SET | The requested parameter does not exist. |
| 7011 | Invalid type | ADD, SET | The supplied value has an invalid type. |
| 7012 | Invalid value | ADD, SET | The supplied value is outside the permitted range. |
| 7013 | Non-writable parameter | ADD, SET | The parameter cannot be modified. |
| 7014 | Value conflict | ADD, SET | The requested value conflicts with another configuration value. |
| 7015 | Operation error | ADD, SET, DELETE | General object operation failure. |
| 7016 | Object does not exist | ADD, SET | The requested object does not exist. |
| 7017 | Object could not be created | ADD | Object creation failed. |
| 7018 | Object is not a table | ADD | The requested path is not a multi-instance object. |
| 7019 | Object is not creatable | ADD | The requested object cannot be created. |
| 7020 | Object could not be updated | SET | The requested object could not be updated. |
| 7021 | Required parameter failed | ADD, SET | A required parameter failed to update. |
| 7022 | Command failure | OPERATE | An operation failed to complete. |
| 7023 | Command canceled | OPERATE | An asynchronous operation was canceled. |
| 7024 | Delete failure | DELETE | Object deletion failed. |
| 7025 | Duplicate key | ADD | An object with the requested unique keys already exists. |
| 7026 | Invalid path | Any | The supplied path does not match the supported data model. |
| 7027 | Invalid command arguments | OPERATE | Operation arguments were invalid or unknown. |
| 7100–7199 | USP Record errors | — | Message Transfer Protocol errors. |
| 7800–7999 | Vendor-defined | — | Vendor-defined USP errors. |

See the Broadband Forum USP specification for the authoritative protocol definition.

---

# 🔌 Language Interoperability

The primary public interface is the C ABI.

This makes the library suitable for integration with languages that provide C FFI support.

Potential integrations include:

| Language | Integration | Status |
|---|---|:---:|
| C | Native C ABI | ✅ |
| C++ | Native C ABI | ✅ |
| Python | CFFI / ctypes / extension | 🚧 |
| Go | cgo | 🚧 |
| Rust | C FFI | 🚧 |
| Java | JNI / Panama | 🚧 |
| C# | P/Invoke | 🚧 |

Language-specific bindings should remain thin wrappers around the stable C ABI.

---

# 🛡️ API & ABI Stability

The public C API is designed to be stable across library releases.

The project avoids exposing C++ ABI details publicly.

In particular, consumers should not need to know about:

- C++ classes
- STL containers
- Allocators
- Exceptions
- Templates
- C++ object layouts
- Internal thread implementations
- Coroutine implementations

This allows the internal implementation to evolve without unnecessarily breaking downstream applications.

---

# 📋 Standards

All implementation work must follow the project's engineering standards.

See:

**[`AGENTS.md`](AGENTS.md)**

The document defines requirements for:

- C++20 usage
- C ABI design
- Memory safety
- Ownership
- RAII
- Thread management
- Coroutines
- Error handling
- ABI stability
- API evolution
- Testing
- Sanitizers
- Build systems
- Code review
- Automated coding agents

---

# 📄 USP MTP Specification Subtree

The `specification/mtp` directory contains the USP MTP specification sourced
from the [BroadbandForum/usp](https://github.com/BroadbandForum/usp) repository
via `git subtree`. The contents correspond to the
[`specification/mtp`](https://github.com/BroadbandForum/usp/tree/master/specification/mtp)
directory in that repository.

The subtree is read-only in this repository — upstream contributions are not
expected.

## One-time setup

Add the upstream remote once per local clone:

```sh
git remote add usp-spec https://github.com/BroadbandForum/usp.git
git fetch usp-spec --tags
```

## Updating to a new upstream tag

Always update to a specific upstream release tag rather than pulling from
`master` directly. This makes the update reproducible and auditable.

1. Fetch the latest tags from the upstream remote:

   ```sh
   git fetch usp-spec --tags
   ```

2. List available tags to choose a target version:

   ```sh
   git tag | sort -V
   ```

3. Create a local split branch that contains only the `specification/mtp`
   subtree history at the chosen tag (replace `<tag>` with the desired version,
   e.g. `v1.5.0`):

   ```sh
   git checkout -b usp-spec-tmp <tag>
   git subtree split --prefix=specification/mtp -b usp-mtp-split
   git checkout copilot/add-usp-mtp-specification-as-subtree  # or your working branch
   ```

4. Merge the new split branch into the subtree prefix:

   ```sh
   git subtree merge --prefix=specification/mtp usp-mtp-split --squash
   ```

5. Clean up the temporary branches:

   ```sh
   git branch -D usp-spec-tmp usp-mtp-split
   ```

6. Commit and push the result.

The squash merge records a single commit in this repository's history for each
upstream update, keeping the log readable.

---

# 🤝 Contributing

Contributions are welcome.

Before submitting a change:

1. Read [`AGENTS.md`](AGENTS.md).
2. Build the project.
3. Run the test suite.
4. Run relevant sanitizer configurations.
5. Add tests for new behavior.
6. Update public API documentation when necessary.
7. Verify that C ABI compatibility has not been unintentionally changed.

Please keep pull requests focused and avoid mixing unrelated refactoring with functional changes.

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the contribution workflow.

---

# 🗺️ Roadmap

### Current

- [x] C-compatible public API
- [x] C++20 implementation
- [x] USP GET
- [x] USP SET
- [x] USP OPERATE
- [x] USP subscriptions
- [x] UDS transport
- [x] Static library
- [x] Shared library

### Next

- [ ] REGISTER
- [ ] ADD
- [ ] DELETE
- [ ] GET_SUPPORTED_DM
- [ ] GET_INSTANCES
- [ ] GET_SUPPORTED_PROTOCOL
- [ ] Expanded transport support
- [ ] Comprehensive C examples
- [ ] Comprehensive C++ examples
- [ ] Sanitizer CI
- [ ] API/ABI compatibility CI
- [ ] Package-manager support
- [ ] API documentation site

---

# ⭐ Project Status

> **Early development**

The core controller functionality is operational, but the API and implementation are still evolving.

The stable C ABI is intended to provide a durable integration point while the internal implementation continues to mature.

Production users should pin a known library version and review the release notes before upgrading.

---

# 📜 License

**PLACEHOLDER**

This project is licensed under the **[LICENSE NAME]** license.

See [`LICENSE`](LICENSE) for the complete license text.

---

# 🙏 Acknowledgements

This project implements functionality based on the **Broadband Forum USP (TR-369)** specifications.

Thanks to the open-source broadband and embedded networking communities for their work on USP implementations and tooling.

---

<div align="center">

## Built for systems that need to talk USP.

**C++20 · C ABI · USP / TR-369**

<br />

[![GitHub stars](https://img.shields.io/github/stars/kartikg33/libuspmtp?style=for-the-badge&logo=github)](#)
[![GitHub forks](https://img.shields.io/github/forks/kartikg33/libuspmtp?style=for-the-badge&logo=github)](#)
[![GitHub issues](https://img.shields.io/github/issues/kartikg33/libuspmtp?style=for-the-badge&logo=github)](#)
[![GitHub license](https://img.shields.io/github/license/kartikg33/libuspmtp?style=for-the-badge)](#)

<br />

**[Documentation](#) · [Examples](#) · [Discussions](#) · [Issues](#)**

</div>