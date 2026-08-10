# AGENTS.md

# C++20 Library Engineering Standards

## Purpose

This document defines the mandatory engineering, architecture, API, ABI, memory-safety, concurrency, testing, and build standards for this project.

These standards apply to:

- Human contributors
- Maintainers
- Code reviewers
- Automated coding agents
- AI coding assistants
- Generated code that becomes part of the project

The project is implemented internally using **modern C++20** and exposes a **stable C-compatible public API**.

The C ABI is the primary interoperability boundary.

All implementation decisions must preserve:

1. Correctness
2. Memory safety
3. Thread safety
4. API stability
5. ABI stability
6. Deterministic resource ownership
7. Portability
8. Testability
9. Maintainability

When these goals conflict, correctness and safety take precedence over convenience or micro-optimization.

---

# Specification Source of Truth (MTP/USP)

This repository includes vendored MTP/USP reference material under:

- `proto/`
- `specification/`

These directories are the authoritative upstream specification inputs for this project.

Before making implementation changes, agents must read:

- `proto/*.proto`
- relevant files under `specification/`

Mandatory rules:

- Never modify files under `proto/` or `specification/` as part of normal implementation work.
- Always treat `proto/` and `specification/` as source-of-truth references for protocol behavior.
- Implementations in this repository must exactly and correctly follow those specifications.
- This codebase is a clean-room C++20 implementation and must not drift from the vendored spec/proto definitions.

---

# 1. General Engineering Principles

The implementation must follow these principles:

- Prefer simple designs over clever designs.
- Prefer explicit ownership over implicit ownership.
- Prefer RAII over manual resource management.
- Prefer value semantics where practical.
- Minimize shared mutable state.
- Minimize global state.
- Keep public interfaces small and stable.
- Keep implementation details private.
- Do not expose C++ implementation details through the C ABI.
- Do not introduce undefined behavior for performance.
- Do not rely on undocumented compiler behavior.
- Do not rely on undocumented platform behavior.
- Do not make breaking API or ABI changes without an explicit architectural decision.
- Do not add abstractions without a concrete reason.
- Do not introduce concurrency without a demonstrated need.
- Do not introduce coroutines merely because C++20 supports them.

Code should be readable by an experienced C++ developer who was not involved in writing it.

---

# 2. C++ Standard

The implementation targets **C++20**.

Use C++20 language and standard-library facilities where they materially improve:

- Safety
- Correctness
- Expressiveness
- Maintainability
- Concurrency
- Resource management

Preferred facilities include, where appropriate:

```cpp
std::unique_ptr
std::shared_ptr
std::weak_ptr
std::span
std::string
std::string_view
std::vector
std::array
std::optional
std::variant
std::expected
std::chrono
std::atomic
std::jthread
std::stop_token
std::mutex
std::shared_mutex
std::condition_variable
std::scoped_lock
std::lock_guard
std::unique_lock
```

Use the smallest appropriate abstraction.

Do not use a C++20 feature simply because it exists.

---

# 3. Public API Architecture

The project has two distinct layers:

```text
                    Public API
                        │
                        ▼
              C-compatible header
                        │
                        ▼
                 C ABI boundary
                        │
                        ▼
                 C++20 implementation
                        │
             ┌──────────┴──────────┐
             ▼                     ▼
        Internal APIs          Internal types
```

The public API must remain independent of the internal implementation.

The public header must not expose:

- C++ classes
- Templates
- Namespaces
- C++ references
- Function overloading
- STL containers
- `std::string`
- `std::span`
- `std::unique_ptr`
- `std::shared_ptr`
- Exceptions
- Lambdas
- C++20 concepts
- C++-specific ownership mechanisms
- Internal implementation types

The public interface must instead use stable C-compatible constructs.

---

# 4. Public C Header

The public header must compile as both C and C++.

It must be possible to write:

```c
#include "<library>.h"
```

from a C translation unit.

It must also be possible to write:

```cpp
#include "<library>.h"
```

from a C++ translation unit.

The header must use C linkage:

```c
#ifdef __cplusplus
extern "C" {
#endif

/* Public API */

#ifdef __cplusplus
}
#endif
```

The header must not require C++ compilation.

---

# 5. C ABI Design

All exported functions must use a C-compatible ABI.

Use explicit exported symbols.

The project should provide an API visibility macro appropriate to the supported platforms.

For example:

```c
#if defined(_WIN32)
    #if defined(LIBRARY_BUILD_SHARED)
        #if defined(LIBRARY_EXPORTS)
            #define LIBRARY_API __declspec(dllexport)
        #else
            #define LIBRARY_API __declspec(dllimport)
        #endif
    #else
        #define LIBRARY_API
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define LIBRARY_API __attribute__((visibility("default")))
#else
    #define LIBRARY_API
#endif
```

The actual macro must reflect the project's naming and platform requirements.

Do not expose compiler-specific ABI constructs unless they are isolated behind the public API portability layer.

---

# 6. Opaque Types

Complex implementation objects should normally be represented as opaque C types.

For example:

```c
typedef struct library_context library_context;
typedef struct library_object library_object;
```

The structure definitions must remain private.

C callers must interact with such objects through API functions.

For example:

```c
LIBRARY_API library_status
library_object_create(
    library_context* context,
    library_object** object
);

LIBRARY_API void
library_object_destroy(
    library_object* object
);
```

Do not expose C++ class layout through the C ABI.

Do not require C callers to know how an object is implemented.

---

# 7. Ownership and Lifetime

Ownership must always be explicit.

For every pointer crossing the C ABI, determine:

- Who owns the object?
- Who destroys it?
- When does ownership transfer?
- How long is the pointer valid?
- Is the pointer nullable?
- Is the pointee mutable?
- Can it be accessed concurrently?
- Can callbacks retain it?
- Can asynchronous work retain it?

These rules must be documented.

Never leave ownership ambiguous.

---

# 8. C ABI Destruction

Every publicly creatable opaque object must have an explicit destruction function unless its lifetime is intentionally managed by another object.

For example:

```c
LIBRARY_API void library_object_destroy(
    library_object* object
);
```

Destruction functions should normally accept `NULL` safely where doing so does not conceal programming errors.

Do not require C consumers to:

- call C++ destructors
- use `delete`
- know allocation details
- use the same allocator as the library
- understand C++ ownership types

Objects allocated by the library must be released through the library's API.

---

# 9. Internal C++ Ownership

Inside the implementation:

### Prefer

```cpp
std::unique_ptr<T>
```

for exclusive ownership.

Use:

```cpp
std::shared_ptr<T>
```

only when ownership is genuinely shared.

Use:

```cpp
std::weak_ptr<T>
```

to represent non-owning relationships where shared ownership would otherwise create cycles or unnecessary lifetime extension.

Raw pointers may represent non-owning relationships, but this must be obvious from the surrounding design.

### Avoid

```cpp
new T(...)
delete ptr;
```

for ordinary object management.

Manual memory management requires explicit justification.

---

# 10. RAII

Resources must normally be acquired and released through RAII.

This includes:

- Memory
- File handles
- Sockets
- Mutexes
- Threads
- OS handles
- Database connections
- Event registrations
- Coroutine resources
- Temporary resources

A class owning a resource should release that resource in its destructor.

Destructors should not normally throw.

Prefer:

```cpp
class Resource {
public:
    Resource();
    ~Resource();

    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;

private:
    ResourceHandle handle_;
};
```

over manual lifecycle protocols.

---

# 11. Rule of Zero

Prefer the Rule of Zero.

Classes should generally rely on their member types to manage resources rather than implementing custom:

- Destructors
- Copy constructors
- Copy assignment
- Move constructors
- Move assignment

When custom special members are required, document why.

---

# 12. Const Correctness

Use `const` aggressively and correctly.

If a function does not modify an object, it should normally expose that fact.

Prefer:

```cpp
void process(std::span<const std::byte> data);
```

over:

```cpp
void process(std::span<std::byte> data);
```

when mutation is unnecessary.

Do not use `const_cast` unless there is a well-understood and documented reason.

---

# 13. References and Pointers

Use references when:

- Null is not a valid state
- The object is guaranteed to exist
- Ownership is not transferred

Use pointers when:

- Null has semantic meaning
- The object is optional
- The pointer represents a non-owning relationship
- Interoperating with the C ABI

Do not use raw pointers as an implicit ownership mechanism.

---

# 14. Strings

Internally, use appropriate C++ string types.

Prefer:

```cpp
std::string
```

for owned strings.

Prefer:

```cpp
std::string_view
```

for non-owning string views where the lifetime is guaranteed.

Do not return a `std::string_view` whose underlying string may be destroyed or modified.

At the C ABI boundary, use appropriate C representations such as:

```c
const char*
```

with explicitly documented encoding and lifetime.

Unless otherwise specified by the API, textual data should use UTF-8.

---

# 15. Buffers

Binary data crossing the C ABI should normally use:

```c
const void* data;
size_t size;
```

or:

```c
void* data;
size_t size;
```

depending on mutability.

Internally, prefer:

```cpp
std::span<const std::byte>
```

or:

```cpp
std::span<std::byte>
```

where appropriate.

Never assume a buffer is null terminated unless the API explicitly guarantees it.

Always validate:

- Pointer validity
- Buffer size
- Integer overflow
- Alignment where relevant
- Required minimum size

---

# 16. Integer and Size Types

Use fixed-width integer types where the width is part of the API contract.

Use:

```c
uint32_t
int64_t
```

and similar types where appropriate.

Use:

```c
size_t
```

for object and buffer sizes.

Avoid using `int` for sizes where overflow or platform width matters.

Do not perform unchecked arithmetic on externally supplied sizes.

---

# 17. Public Structures

Public C structures must contain only C-compatible fields.

Simple data structures may be exposed directly:

```c
typedef struct library_config {
    int enabled;
    size_t buffer_size;
} library_config;
```

Complex objects should be opaque.

If ABI evolution is expected, consider versioning public structures:

```c
typedef struct library_config {
    size_t struct_size;
    uint32_t version;

    /* fields */
} library_config;
```

Use structure versioning deliberately rather than mechanically.

---

# 18. Enumerations

Simple enumerations may use C enums:

```c
typedef enum library_status {
    LIBRARY_OK = 0,
    LIBRARY_ERROR = 1,
    LIBRARY_INVALID_ARGUMENT = 2
} library_status;
```

Do not represent a data-carrying variant as a simple enum.

Use a tagged representation when the API needs associated data.

For example:

```c
typedef enum library_value_kind {
    LIBRARY_VALUE_NONE,
    LIBRARY_VALUE_INTEGER,
    LIBRARY_VALUE_STRING
} library_value_kind;

typedef struct library_value {
    library_value_kind kind;

    union {
        int64_t integer_value;
        const char* string_value;
    };
} library_value;
```

The representation must make invalid states difficult to create.

---

# 19. Error Handling

C++ exceptions must never cross the C ABI.

Every exported function must prevent exceptions from escaping.

The implementation should catch exceptions at the ABI boundary and translate them into the project's C error representation.

The public API should use a documented error model such as:

```c
library_status function(...);
```

or:

```c
library_status function(..., library_error** error);
```

depending on the API.

Errors must not be silently discarded.

If an error contains structured information internally, preserve useful information through the public API.

---

# 20. Exception Policy

Exceptions may be used internally where they improve correctness and maintainability, subject to project policy.

However:

- Exceptions must never escape exported C functions.
- Destructors must not throw.
- Worker threads must not terminate the process due to an uncaught exception.
- Coroutine execution must not allow unhandled exceptions to escape into undefined territory.
- Exceptions must be translated into appropriate API-level errors.

If the project adopts a no-exceptions policy, follow that project-wide policy instead.

---

# 21. Thread Safety

Every public object and operation must have a clearly understood concurrency contract.

Classify APIs as appropriate:

- Thread-safe
- Thread-compatible
- Single-threaded
- Internally synchronized
- Externally synchronized

Document the classification where it is not obvious.

Do not claim thread safety merely because an object happens to use a mutex internally.

---

# 22. Synchronization

Use the narrowest synchronization mechanism appropriate to the problem.

Preferred tools include:

```cpp
std::mutex
std::shared_mutex
std::scoped_lock
std::lock_guard
std::unique_lock
std::condition_variable
std::atomic
```

Avoid holding locks while performing:

- User callbacks
- Potentially blocking I/O
- Long-running computation
- Unbounded waits
- Operations that may re-enter the library

unless explicitly required.

Design locking around invariants rather than sprinkling mutexes throughout the implementation.

---

# 23. Atomics

Use atomics only when their semantics are understood.

Do not use an atomic merely to eliminate a compiler warning.

For every non-trivial atomic operation, understand:

- Atomicity
- Ordering
- Synchronization
- Lifetime
- ABA risks where applicable

Use the weakest memory ordering that correctly expresses the required synchronization, but prioritize correctness and clarity over micro-optimization.

---

# 24. Thread Management

Prefer:

```cpp
std::jthread
```

over raw:

```cpp
std::thread
```

where appropriate.

Worker threads must have explicit ownership and lifetime.

Avoid detached threads.

A thread must not outlive the objects it accesses.

Destruction must safely coordinate with worker shutdown.

Use:

```cpp
std::stop_token
```

for cooperative cancellation where appropriate.

Thread shutdown must be:

- deterministic
- race-free
- idempotent where practical
- safe during partial initialization
- safe during error handling

---

# 25. Thread Exceptions

Exceptions escaping a worker thread must never cause uncontrolled process termination.

Worker-thread entry points must establish an appropriate exception boundary.

Errors should be propagated through the library's established error mechanism.

---

# 26. Callbacks

Callbacks crossing the C ABI must use C-compatible function pointers.

For example:

```c
typedef void (*library_event_callback)(
    void* user_data,
    const library_event* event
);
```

The callback API must explicitly define:

- Whether `user_data` is borrowed or owned
- Callback lifetime
- Callback thread
- Callback ordering
- Whether callbacks may be concurrent
- Whether callbacks may re-enter the library
- Whether callbacks may block
- Whether callbacks may retain passed pointers
- How callback shutdown is handled

Never invoke a callback after its associated `user_data` has become invalid.

---

# 27. Callback Reentrancy

Assume user callbacks may behave unpredictably unless the API explicitly restricts them.

Avoid invoking callbacks while holding internal locks.

If callbacks may re-enter the library, the implementation must be designed to tolerate the documented reentrancy.

If reentrancy is prohibited, document and enforce that contract.

---

# 28. Coroutines

C++20 coroutines may be used where they provide a clear benefit for:

- Asynchronous I/O
- Event-driven operations
- Cooperative scheduling
- Structured asynchronous workflows
- Cancellation-aware asynchronous operations

Do not use coroutines for ordinary synchronous code.

Do not expose implementation-specific coroutine types through the C ABI.

The public C API should expose an ABI-safe abstraction such as:

- Callback-based completion
- Opaque operation handles
- Polling
- Explicit start/stop operations

---

# 29. Coroutine Lifetime

Coroutine lifetime must be explicit.

Ensure that:

- Coroutine frames cannot outlive required state.
- Awaited objects remain valid for the required duration.
- Cancellation is safe.
- Completion handlers remain valid.
- Destruction correctly cancels or awaits outstanding operations.
- Exceptions are handled.
- No callback is invoked after its owner has been destroyed.

Never rely on accidental lifetime extension.

---

# 30. Asynchronous Operations

Every asynchronous operation must define:

- Start semantics
- Completion semantics
- Cancellation semantics
- Failure semantics
- Ownership
- Callback lifetime
- Thread/executor semantics
- Shutdown behavior

Do not leave background operations running after their owning object has been destroyed unless the API explicitly specifies that behavior.

---

# 31. Channels and Queues

When communicating between threads, use an appropriate synchronized queue/channel abstraction.

The implementation must define:

- Ownership of queued objects
- Queue capacity
- Blocking behavior
- Shutdown behavior
- Cancellation
- Producer behavior after shutdown
- Consumer behavior after shutdown

Avoid unbounded queues unless unbounded growth is intentional and safe.

---

# 32. Deadlocks

Design to prevent deadlocks rather than attempting to detect them after the fact.

Avoid:

- Lock-order inversions
- Calling external code while holding locks
- Recursive locking unless intentional
- Waiting on a thread while holding a lock required by that thread
- Destruction while holding locks needed for cleanup

Where multiple locks are required, establish and document a consistent lock ordering.

Use:

```cpp
std::scoped_lock
```

for acquiring multiple mutexes safely where appropriate.

---

# 33. Data Races

Data races are defects.

Do not rely on:

- "It usually works."
- Platform-specific scheduling behavior
- Timing assumptions
- Sleep calls
- Volatile variables

`volatile` must not be used as a synchronization primitive.

Use appropriate atomics or synchronization mechanisms.

---

# 34. Global State

Avoid mutable global state.

If global state is necessary:

- Make initialization thread-safe.
- Define destruction behavior.
- Define ownership.
- Define shutdown semantics.
- Consider dynamic-library unload behavior.

Prefer explicit library/context objects where practical.

---

# 35. Initialization and Shutdown

Library initialization and shutdown must be deterministic.

If explicit initialization is required, document:

```text
initialize → use → shutdown
```

If initialization is automatic, ensure static initialization order cannot create problems.

Shutdown must safely coordinate:

- Worker threads
- Callbacks
- Coroutines
- Queues
- OS resources
- Registered handlers

Repeated shutdown should be safe where practical.

---

# 36. Dynamic Library Safety

The implementation must be safe when built as a shared library.

Do not assume that:

- Application and library use the same allocator
- Application and library use the same C++ runtime configuration
- Application and library use the same compiler version
- C++ objects can safely cross the ABI

This is one reason the public boundary is C.

Memory allocated by the library must normally be freed through the library.

---

# 37. Static Library Safety

The same public ABI must work when the implementation is built as a static library.

Do not introduce behavior that depends on dynamic-library loading unless explicitly required.

Build configuration must clearly distinguish static and shared builds where necessary.

---

# 38. ABI Stability

The public C ABI should be treated as stable.

Avoid changing:

- Function signatures
- Structure layouts
- Enum values
- Calling conventions
- Ownership rules
- String encoding
- Error semantics

without an intentional API/ABI versioning decision.

Never reorder or remove public enum values without considering ABI compatibility.

Never change the size or layout of an exposed structure casually.

---

# 39. API Evolution

Prefer additive API changes.

When functionality must evolve:

- Add new functions.
- Preserve existing functions.
- Preserve existing enum values.
- Preserve existing ownership semantics.
- Preserve existing behavior.

If breaking changes are unavoidable, explicitly version the API.

---

# 40. Compatibility

The public header should remain usable by:

- C applications
- Modern C++ applications
- Legacy C++ applications
- Legacy C applications

The implementation itself requires C++20.

The public header must not unnecessarily require modern C++ language features.

Where practical, keep the public header conservative and portable.

---

# 41. Compiler Portability

Support the project's declared compiler matrix.

At minimum, consider:

- GCC
- Clang
- MSVC

Do not rely on compiler extensions when standard C++20 provides an adequate solution.

When platform-specific functionality is necessary, isolate it behind a small internal abstraction.

---

# 42. Platform-Specific Code

Platform-specific code should be isolated.

Prefer structures such as:

```text
src/
    platform/
        posix/
        windows/
```

or another architecture appropriate to the project.

Do not spread platform-specific `#ifdef` blocks throughout unrelated business logic.

---

# 43. Preprocessor Usage

Keep preprocessor usage minimal.

Use `#if` primarily for:

- Platform differences
- Compiler differences
- ABI/export configuration
- Feature detection
- Build configuration

Do not use macros where ordinary C++ constructs are clearer.

Public macros must have project-specific prefixes.

---

# 44. Header Hygiene

Every public header must be self-contained.

This must compile:

```cpp
#include "<library>.h"

int main() {}
```

and the equivalent C source.

Do not rely on another header being included first.

Public headers should include only what they actually require.

Keep transitive dependencies minimal.

---

# 45. Include Discipline

Prefer:

```cpp
#include <...>
```

for standard-library headers.

Use project-relative includes consistently.

Avoid unnecessary includes.

Prefer forward declarations where they materially reduce coupling and are valid.

Do not forward-declare standard-library entities.

---

# 46. Namespaces

Internal C++ implementation code should use an appropriate project namespace.

Do not use:

```cpp
using namespace std;
```

Do not place project symbols in the global C++ namespace unless explicitly required.

The C API is intentionally global because it is an ABI boundary.

---

# 47. Naming

Use consistent project naming.

Public C API names should use a project-specific prefix:

```c
library_object_create
library_object_destroy
library_status
```

Avoid generic public symbols such as:

```c
create
destroy
status
context
```

Internal C++ names should follow the project's established naming convention.

Do not introduce a new naming convention without a reason.

---

# 48. Classes

Classes should have a single clear responsibility.

Prefer composition over inheritance.

Use inheritance primarily for:

- Stable polymorphic interfaces
- Ownership models that genuinely require polymorphism
- Well-defined substitution relationships

Do not use inheritance simply for code reuse.

Prefer `final` when a polymorphic class is not intended to be extended.

---

# 49. Virtual Interfaces

Polymorphic base classes should have virtual destructors when they are intended to be deleted through the base interface.

Avoid unnecessary virtual dispatch in performance-critical paths.

Never expose C++ virtual interfaces through the C ABI.

Translate public polymorphism into a C-compatible representation such as a vtable and opaque context where appropriate.

---

# 50. Templates

Templates should remain implementation details unless there is a compelling reason to expose them in a C++-specific API.

Do not expose templates through the C ABI.

Avoid excessive template metaprogramming.

Prefer straightforward, readable code.

---

# 51. STL Containers

STL containers are encouraged internally.

They must not cross the C ABI.

For example, internally:

```cpp
std::vector<std::byte>
```

is appropriate.

Publicly:

```c
const void* data;
size_t size;
```

may be appropriate.

---

# 52. Optional Values

Use:

```cpp
std::optional<T>
```

internally when a value may legitimately be absent.

Do not represent optionality through ambiguous sentinel values unless required by the public C ABI.

At the C boundary, use:

- nullable pointers
- explicit validity fields
- status values
- tagged structures

as appropriate.

---

# 53. Variant Values

Use:

```cpp
std::variant
```

internally for type-safe alternatives.

Do not expose `std::variant` through the C ABI.

Represent variant data using an explicit C-compatible tagged union.

---

# 54. Integer Overflow

External input must be treated as untrusted.

Check arithmetic involving:

- Buffer sizes
- Allocation sizes
- File sizes
- Network lengths
- Element counts
- Offsets
- Multiplication/addition used for allocation

Never allow integer overflow to result in undersized allocations or out-of-bounds access.

---

# 55. Input Validation

Validate public API inputs.

Check:

- Null pointers
- Sizes
- Ranges
- Enum values
- Object state
- Required initialization
- Ownership assumptions
- Buffer relationships

Do not assume callers are correct merely because the API is intended for trusted applications.

---

# 56. Undefined Behavior

Undefined behavior is unacceptable.

Pay particular attention to:

- Out-of-bounds access
- Use-after-free
- Double-free
- Invalid casts
- Misaligned access
- Signed integer overflow
- Uninitialized memory
- Data races
- Invalid object lifetime
- Dangling references
- Invalid function pointers
- Calling functions through incompatible types

When uncertain, choose the safer design and verify with tests and sanitizers.

---

# 57. Type Safety

Prefer strong types internally.

Do not represent semantically different values with the same primitive type when a strong type improves correctness.

For example, consider dedicated types for:

- IDs
- Handles
- Sizes
- Durations
- Flags

The public C API may necessarily use primitive C types, but the C++ implementation should preserve type distinctions where useful.

---

# 58. Boolean and Flags

Internally, use:

```cpp
bool
```

for boolean state.

For C ABI compatibility, use a representation appropriate to the public header's compatibility requirements.

Flags should use explicit bit definitions rather than overloaded magic integers.

---

# 59. Magic Numbers

Do not use unexplained numeric constants.

Prefer:

```cpp
constexpr
```

or named enums/constants.

Public constants must have stable names and documented meanings.

---

# 60. Logging

Library code must not unexpectedly write to:

- stdout
- stderr
- application logs

unless explicitly documented.

Prefer an injectable logging/callback mechanism where logging is required.

Do not make logging a hidden source of synchronization or performance problems.

---

# 61. File and Resource Handling

All OS resources must have deterministic ownership.

Use RAII wrappers.

Do not leak resources on:

- exceptions
- early returns
- cancellation
- thread shutdown
- partial initialization
- failed construction

---

# 62. Performance

Correctness and safety come before optimization.

After correctness is established, profile before optimizing.

Pay particular attention to:

- Allocation frequency
- Copies
- Lock contention
- Thread creation
- Coroutine scheduling
- I/O
- Serialization
- Cache behavior
- Hot loops

Do not optimize based solely on intuition.

Do not introduce unsafe optimizations without measurement and justification.

---

# 63. Zero-Copy APIs

Where performance requires zero-copy behavior, explicitly document:

- Buffer ownership
- Buffer lifetime
- Mutability
- Thread safety
- Alignment
- Required synchronization

Do not introduce zero-copy interfaces that create ambiguous lifetime requirements.

---

# 64. Testing

Every meaningful feature must have tests.

Tests should cover:

- Normal operation
- Invalid input
- Error handling
- Boundary conditions
- Ownership
- Destruction
- Repeated initialization/shutdown
- Concurrency
- Cancellation
- Callbacks
- Asynchronous behavior
- Resource cleanup

Tests must verify observable behavior rather than implementation details wherever practical.

---

# 65. C API Tests

The public API must be tested from C.

At minimum, compile test programs that:

```c
#include "<library>.h"
```

using a C compiler.

Verify that C callers can:

- Create objects
- Use objects
- Receive errors
- Register callbacks
- Destroy objects
- Use asynchronous APIs where applicable

---

# 66. C++ API Tests

The C header must also be tested from C++.

Verify that:

```cpp
#include "<library>.h"
```

compiles cleanly under C++20.

Where appropriate, test with more than one supported C++ compiler.

---

# 67. Static and Shared Library Tests

Both build modes must be tested.

At minimum:

```text
Static library
    C consumer
    C++ consumer

Shared library
    C consumer
    C++ consumer
```

Do not consider the project complete if only one library mode works.

---

# 68. Sanitizers

The project should support:

- AddressSanitizer
- UndefinedBehaviorSanitizer
- ThreadSanitizer where supported

Sanitizer builds must run the test suite.

The goal is zero sanitizer findings.

Do not suppress sanitizer errors without documenting and justifying the suppression.

---

# 69. Static Analysis

Use appropriate compiler warnings and static analysis.

Treat warnings seriously.

Do not disable warnings merely to make the build pass.

Warnings should be fixed at the source wherever practical.

---

# 70. Concurrency Testing

Concurrency tests must attempt to expose:

- Data races
- Deadlocks
- Lost wakeups
- Shutdown races
- Callback races
- Lifetime races
- Cancellation races
- Queue shutdown bugs

Avoid relying exclusively on sleeps to reproduce timing-sensitive bugs.

Prefer synchronization barriers, deterministic test hooks, and controlled scheduling where practical.

---

# 71. Build System

Use CMake unless the project has an established build system that intentionally replaces it.

The build system should support:

```text
Debug
Release
Static
Shared
Tests
Examples
Sanitizers
```

as appropriate.

Consumers should be able to use an installed package through a conventional CMake target.

For example:

```cmake
find_package(<library> CONFIG REQUIRED)

target_link_libraries(my_app PRIVATE <library>::<library>)
```

Use the project's actual target name.

---

# 72. Installation Layout

Use a conventional layout:

```text
include/
lib/
bin/
```

as appropriate for the target platform.

Only public headers should be installed as public API.

Do not install internal headers unless explicitly required.

---

# 73. Build Reproducibility

Builds should be deterministic where practical.

Avoid:

- Undocumented environment dependencies
- Absolute source paths embedded in artifacts
- Uncontrolled generated files
- Network access during ordinary builds unless explicitly required

Pin external dependencies appropriately.

---

# 74. Dependencies

Minimize dependencies.

Before introducing a dependency, consider:

- Whether the standard library provides the required functionality
- Maintenance burden
- ABI implications
- License
- Security
- Platform support
- Build complexity
- Binary size

Do not add a dependency merely to avoid writing a small, well-understood abstraction.

---

# 75. Public API Documentation

Every public function must document:

- Purpose
- Parameters
- Return value
- Ownership
- Lifetime
- Thread safety
- Blocking behavior
- Callback behavior
- Error behavior
- Cancellation behavior where applicable

Documentation must be understandable without reading the implementation.

---

# 76. ABI Documentation

Document ABI assumptions where relevant.

The public API documentation should make clear:

- Calling convention
- Ownership
- Allocator expectations
- String encoding
- Structure layout expectations
- Thread-safety guarantees
- Callback rules
- Library initialization requirements

---

# 77. Source Code Documentation

Comment **why**, not merely **what**.

Good:

```cpp
// Release the lock before invoking the callback because callbacks may
// synchronously re-enter the library.
```

Bad:

```cpp
// Unlock mutex.
```

Do not add comments that merely restate obvious code.

---

# 78. TODOs

TODO comments should contain actionable information.

Prefer:

```cpp
// TODO(#123): Replace polling with event-driven notification once the
// platform backend exposes the required primitive.
```

over:

```cpp
// TODO: improve this
```

Do not leave vague TODOs in production code.

---

# 79. Generated Code

Generated code must be clearly identified.

Do not manually edit generated files unless the generation process explicitly requires it.

Changes to generated output should normally be accompanied by changes to the generator or source definition.

---

# 80. API Review

Before adding or changing public API, verify:

- Is the functionality necessary?
- Is the naming consistent?
- Is ownership explicit?
- Is the lifetime clear?
- Is it ABI-safe?
- Is it C-compatible?
- Is it thread-safe or explicitly documented otherwise?
- Can the API evolve without breaking ABI?
- Can it be implemented portably?
- Is error handling complete?

Public APIs should be difficult to misuse.

---

# 81. Breaking Changes

Do not make breaking API or ABI changes casually.

A breaking change includes:

- Removing a public function
- Changing a function signature
- Changing ownership semantics
- Changing callback semantics
- Changing structure layout
- Changing enum values
- Changing string encoding
- Changing thread-safety guarantees
- Changing destruction semantics

Breaking changes require explicit maintainer approval and appropriate versioning.

---

# 82. Security

Treat externally supplied data as untrusted.

Pay particular attention to:

- Buffer lengths
- Integer overflow
- Resource exhaustion
- Malformed input
- Race conditions
- Unsafe deserialization
- Path handling
- Network data
- Callback abuse
- Unbounded queues

Do not trade security for convenience.

---

# 83. Resource Exhaustion

Asynchronous systems must have bounded resource behavior where practical.

Consider limits for:

- Threads
- Queues
- Buffers
- Outstanding operations
- Coroutine tasks
- Connections
- Memory

Do not allow an untrusted caller to cause unbounded resource growth unless explicitly required.

---

# 84. Shutdown Semantics

Shutdown must be treated as a first-class state.

Every asynchronous subsystem must define:

```text
running
stopping
stopped
```

or an equivalent state model.

Operations initiated during shutdown must have deterministic behavior.

Do not allow shutdown to race indefinitely with new work.

---

# 85. Object State Machines

For complex objects, explicitly model valid states.

For example:

```text
Created
    ↓
Initialized
    ↓
Running
    ↓
Stopping
    ↓
Stopped
```

Public operations must reject invalid state transitions predictably.

Avoid scattered boolean flags when a clear state model is more appropriate.

---

# 86. Reentrancy

Assume externally supplied callbacks can re-enter the library unless the API explicitly prohibits it.

Avoid holding internal locks across external calls.

Document reentrancy restrictions.

---

# 87. API Return Values

Return values must communicate success or failure clearly.

Avoid APIs where:

```c
NULL
```

could mean several unrelated things.

Prefer explicit status values when ambiguity would otherwise exist.

---

# 88. Null Handling

Public C APIs should define whether pointer arguments may be null.

If null is invalid, validate it and return an appropriate error.

If null has meaning, document it.

Internal C++ code should prefer references and strong types when null is not meaningful.

---

# 89. Resource Cleanup on Failure

Every partially constructed operation must clean up safely.

This applies to:

- Object creation
- Thread startup
- Coroutine creation
- Queue registration
- Callback registration
- File/socket opening
- Multi-stage initialization

RAII should make failure cleanup automatic wherever possible.

---

# 90. Testing Failure Paths

Tests must cover failure paths, not just successful execution.

Include:

- Invalid arguments
- Allocation failures where testable
- Initialization failures
- Shutdown during active work
- Callback failure
- Cancellation
- Thread startup failure
- I/O errors
- Malformed input

---

# 91. Examples

Examples should demonstrate correct API usage.

Provide examples for:

```text
C
C++
Static linking
Shared linking
Basic lifecycle
Error handling
Callbacks
Asynchronous operations
Shutdown
```

where applicable.

Examples must not demonstrate unsafe ownership patterns.

---

# 92. Legacy Compatibility

The public header must remain conservative enough for the declared legacy compatibility target.

Do not unnecessarily require:

- Modern C++ syntax
- C++ standard-library headers
- C++ keywords
- C++ templates
- C++ namespaces

The implementation remains C++20.

The compatibility requirement applies primarily to the public C header and ABI.

---

# 93. Code Review Standards

Reviewers should ask:

### Correctness

- Does this preserve intended behavior?
- Are edge cases handled?
- Are failure paths safe?

### Memory safety

- Is ownership explicit?
- Can anything dangle?
- Can anything double-free?
- Can a callback outlive its context?

### Concurrency

- Are there races?
- Can this deadlock?
- Can shutdown race with execution?
- Are atomics correctly ordered?

### ABI

- Is the public API C-compatible?
- Does the change affect ABI?
- Are ownership semantics clear?

### Maintainability

- Is the design understandable?
- Is the abstraction necessary?
- Is the implementation consistent with project conventions?

---

# 94. Automated Agent Behavior

Automated coding agents must follow the same standards as human contributors.

Before modifying substantial code, agents should:

1. Inspect the repository.
2. Understand the existing architecture.
3. Identify relevant public APIs.
4. Identify ownership and lifetime relationships.
5. Identify concurrency assumptions.
6. Inspect existing tests.
7. Follow established naming and build conventions.

Agents must not:

- Rewrite large portions of the project without understanding them.
- Remove tests merely because they fail after a change.
- Disable warnings to make builds pass.
- Disable sanitizers to hide failures.
- Introduce unsafe raw ownership for convenience.
- Introduce detached threads without justification.
- Change public ABI casually.
- Replace an asynchronous operation with a blocking operation without explicit justification.
- Remove error handling to simplify code.
- Guess at undocumented behavior when it can be determined from the code and tests.

When requirements are ambiguous, preserve existing behavior and choose the least surprising implementation.

---

# 95. Agent Change Discipline

Agents should make the smallest coherent change necessary.

Prefer:

```text
small change
→ build
→ test
→ inspect
→ continue
```

over:

```text
rewrite everything
→ discover problems afterward
```

Do not mix unrelated refactoring with functional changes unless necessary.

---

# 96. Validation Before Completion

Before declaring a task complete, verify as applicable:

- [ ] Project builds successfully.
- [ ] Public headers compile as C.
- [ ] Public headers compile as C++.
- [ ] Static library builds.
- [ ] Shared library builds.
- [ ] C tests pass.
- [ ] C++ tests pass.
- [ ] Existing tests pass.
- [ ] New tests pass.
- [ ] Sanitizers pass.
- [ ] Compiler warnings are clean.
- [ ] No new data races were introduced.
- [ ] No ownership ambiguities were introduced.
- [ ] No ABI-breaking changes were introduced.
- [ ] Documentation is updated.
- [ ] Examples remain valid.

Do not declare success based solely on compilation.

---

# 97. Final Engineering Rule

When choosing between two otherwise valid implementations, prefer the one that makes:

- Ownership more obvious
- Lifetimes more explicit
- Concurrency easier to reason about
- Failure handling more deterministic
- The C ABI more stable
- The public API harder to misuse
- Tests easier to write
- Future maintenance easier

The goal is not merely to produce code that works.

The goal is to maintain a **safe, predictable, portable, ABI-stable C++20 library that remains understandable and maintainable over its entire lifetime**.