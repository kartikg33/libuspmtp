# Security Policy

## Supported versions

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |

Only the latest release is supported with security fixes.
If you are pinned to an older version, upgrade before reporting —
the issue may already be fixed.

## Reporting a vulnerability

**Do not open a public issue for a suspected vulnerability.**

Use GitHub's
[private vulnerability reporting](https://github.com/kartikg33/libuspmtp/security/advisories/new)
for this repository. Reports stay confidential until a fix is available
and disclosure is coordinated.

Please include, where possible:

- Affected version(s) and commit hash
- The vulnerable component (e.g. protocol parsing, UDS transport,
  C ABI boundary, example code)
- Steps to reproduce, ideally a minimal reproducer or proof of concept
- The impact you see (crash, memory corruption, data leak, bypass, …)
- Whether the issue reproduces under AddressSanitizer /
  UndefinedBehaviorSanitizer, and any sanitizer output

## What to expect

- Acknowledgement of your report
- Investigation and assessment of impact
- A fix released as a new patch version where appropriate
- Credit in the release notes, if you wish

We ask that you give us a reasonable opportunity to fix the issue
before any public disclosure, and that you do not exploit the issue
beyond what is necessary to demonstrate it.

## Scope

This library parses **untrusted data from the network** (USP records
received over the transport) and exposes a C ABI consumed by
potentially long-running processes. The following are in scope and
treated as security issues:

- Memory-safety violations reachable from malformed input
  (out-of-bounds access, use-after-free, double-free, integer
  overflow leading to undersized allocation)
- Data races, deadlocks, or lifetime violations in the library's
  worker threads, callbacks, or shutdown paths
- Acceptance of malformed USP records that should be rejected
- Bypass of agent permission checks enforced client-side, where
  the library claims to enforce them
- Vulnerabilities in the shipped example applications that would
  mislead users about safe API usage

Out of scope: the vendored reference inputs under `proto/` and
`specification/` (upstream material — report upstream defects to
their maintainers), and vulnerabilities in third-party dependencies
such as Protocol Buffers (report those upstream, unless libuspmtp
uses the dependency in an unsafe way).

## Secure development

Contributors are expected to follow [`AGENTS.md`](AGENTS.md), which
requires input validation, checked arithmetic on externally supplied
sizes, no undefined behaviour, and sanitizer-clean test runs.
CI enforces ASan + UBSan on the test suite (see
[README](README.md#-sanitizers)).
