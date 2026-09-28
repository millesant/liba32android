# Contributing to liba32android

Thanks for helping improve liba32android. The project is early, systems-heavy,
and deliberately conservative about compatibility claims, so reproducibility
and clear boundaries matter more than large patches.

## Before you start

Please read:

- [README.md](README.md) for project scope and maturity;
- [ROADMAP.md](ROADMAP.md) for current direction;
- [docs/README.md](docs/README.md) for architecture/development documentation;
- [AGENTS.md](AGENTS.md) for repository engineering invariants.

Human contributors do not need access to the maintainer's private automation
control plane. Everything needed to build, test, and understand public project
scope should be available in this repository.

## Build and test

A normal host build is:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIBA32ANDROID_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

See [docs/development/build-and-test.md](docs/development/build-and-test.md) for
real ARM32 fixture and Android cross-build details.

Before proposing a change, run the narrowest relevant tests and, when practical,
the full host suite. Do not report a test as passing unless it actually ran
against the revision you are submitting.

## Good contributions

Useful contributions include:

- deterministic bug fixes with regressions;
- bounded compatibility additions backed by real Android/AArch32 evidence;
- improvements to ELF/JNI/Bionic behavior that preserve layer boundaries;
- clearer diagnostics and failure classification;
- reproducible fixture/tooling improvements;
- documentation that makes architecture or validation easier to understand.

For compatibility work, include enough evidence to answer: **what real binary or
Android behavior requires this, what is the smallest generic contract, and how
is it regression-tested?**

## Scope rules

Please preserve these invariants:

- keep the runtime game/application agnostic;
- never expose host pointers as logical 32-bit guest pointers;
- keep Dynarmic behind `src/cpu/`;
- use `memory::GuestMemory` as the generic memory seam;
- keep ELF mapping, metadata, linking, relocation, lifecycle, and hardening
  separated;
- do not broaden permissions or silently relax bounds just to make a fixture
  pass;
- avoid freezing private C++ compatibility/linker objects into the public C ABI.

Application-specific experiments should stay outside the generic runtime unless
a reusable contract has been demonstrated.

## Pull requests

Keep patches focused. A strong PR description should include:

1. the problem or compatibility gap;
2. the evidence/reproducer;
3. the chosen boundary/behavior;
4. tests that ran and their result;
5. limitations or intentionally deferred work.

If the change affects architecture or a public-facing workflow, update the
relevant documentation in the same PR.

## Bug reports

Please include:

- exact commit/revision;
- host OS/architecture and compiler;
- Android API/ABI/device information when relevant;
- build configuration;
- smallest reproducer available;
- expected vs observed behavior;
- relevant structured `A32ERR` diagnostics or a short failure-centered log
  excerpt.

Do not attach proprietary APKs/libraries unless you have the right to share
them. Prefer hashes, symbol/ELF metadata, minimal generated fixtures, or
redacted evidence.

## Style

Match the surrounding C/C++ style. Keep ownership, bounds, integer conversions,
ABI layout, and error behavior explicit. New APIs should have deterministic
failure behavior and finite caller-controlled ceilings where untrusted guest
data is involved.

## Licensing

The project license has not yet been selected. Contributions made before a
license is committed do not imply that a particular open-source license already
applies. This is an explicit project-level item that must be resolved before a
formal open-source release.
