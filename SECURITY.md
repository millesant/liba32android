# Security policy

liba32android processes untrusted guest addresses, ELF metadata, strings,
relocations, and executable code. Memory-safety and validation bugs are therefore
treated as security-relevant even while the project is experimental.

## Reporting a vulnerability

Please do **not** publish exploit details in a normal GitHub issue.

Prefer GitHub's private vulnerability-reporting / Security Advisory flow for
this repository when it is available. If private reporting is not available,
open a minimal issue stating that you need a private security contact, without
including exploit details, crash artifacts containing secrets, or proprietary
binaries.

Include, when possible:

- affected commit SHA;
- host/target architecture;
- minimal reproducer or generated fixture;
- expected and observed behavior;
- sanitizer/diagnostic output;
- whether the issue crosses a guest/host memory or execution boundary.

## Supported versions

There are no stable releases yet. Security fixes target the current `main`
integration branch unless a released version is explicitly documented later.

## Scope

Examples of security-sensitive issues include:

- host memory exposure through guest-pointer confusion;
- out-of-bounds guest-memory access;
- integer overflow leading to invalid mapping/range validation;
- malformed ELF metadata escaping configured bounds;
- unsafe permission broadening or executable-memory transitions;
- incorrect host-service validation;
- use-after-free/lifetime errors across loader/runtime state.

General compatibility failures without a security boundary impact can be filed
through the normal bug-report template.
