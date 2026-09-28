---
name: Bug report
about: Report a reproducible runtime, ELF, compatibility, build, or Android validation bug
title: "[bug] "
labels: bug
---

## Revision

Commit SHA:

## Environment

- Host OS/architecture:
- Compiler/CMake:
- Android device/API/ABI (if relevant):
- Build options:

## Reproducer

Describe the smallest reproducible case. Prefer generated fixtures or commands
that can be run from a clean checkout.

## Expected behavior

What should happen?

## Observed behavior

What happened instead?

## Evidence

Include focused diagnostics, `A32ERR` output, ELF metadata, or a short
failure-centered log excerpt.

Do not upload proprietary binaries or sensitive data unless you have the right
to share them.

## Notes

Anything else that narrows the affected layer (CPU, memory, ELF, linker,
compatibility service, JNI, Android harness, public API)?
