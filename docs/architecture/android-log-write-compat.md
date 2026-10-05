# A32 Android `__android_log_write` compatibility service

Status: feature 026 DONE; exact-head implementation CI PASSed at `59fa3eba1d53c6679ef6086cd209198ca7ecb4ac`

## Evidence boundary

The supplied ARM32 FMOD library and VLC ARMv7 `libvlc.so` both import
`__android_log_write`; see
`docs/research/evidence/android-log-imports-2026-09-26.md`.

Authoritative platform/ABI evidence is captured in
`docs/research/evidence/android-log-write-contract-2026-09-26.md`:

- Android declares
  `int __android_log_write(int prio, const char* tag, const char* text)`;
- AAPCS32 assigns the first three 32-bit arguments to r0-r2 and the result to
  r0;
- current Android documentation reports a normal result of 1 when written or
  `-EPERM` when logging policy rejects the message.

## Layering

`src/compat/a32_android_log_write.*` is an Android-specific compatibility
adapter above the game-agnostic feature-024/025 service seams. It contains no
Dynarmic types and does not change ELF, dependency, relocation, or namespace
policy.

The service is configured with:

- one caller-selected exact SVC immediate;
- a caller-owned `A32AndroidLogSink`;
- independent maximum tag and text payload lengths.

The caller-selected service number keeps private guest-shim protocol assignment
outside the generic runtime registry.

## Guest ABI

For a matching SVC:

- r0 is interpreted bit-for-bit as signed 32-bit `prio`;
- r1 is the logical guest pointer to the tag string;
- r2 is the logical guest pointer to the text string;
- the sink's signed 32-bit result is written bit-for-bit back to r0.

A zero tag pointer is preserved as a null tag for the sink, matching Android's
ability to apply default-tag policy. A zero text pointer fails before the sink
is invoked.

Non-null strings are copied byte-for-byte through `GuestMemory` until NUL,
with separate caller-provided maximum payload lengths. The terminating NUL does
not count against the payload ceiling, so a payload exactly at the configured
limit is valid. Unreadable memory, logical-address overflow, or failure to find
NUL at the ceiling returns `Failed` before sink invocation.

## Sink boundary

The sink receives views backed by temporary owned copies valid only for the
synchronous call. It decides host logging/filter behavior and returns the
32-bit result exposed to the guest.

The compatibility service itself does not link to host `liblog`, invent
logging policy, or translate return codes.

## Deliberate exclusions and later extension

Feature 026 itself provides only the bounded `__android_log_write` service;
it does not provide a guest ELF `liblog.so`, select a platform provider, or
decode variadic formatting.

The later bounded liblog extension adds
`__android_log_print`/`__android_log_vprint` in the same compatibility
source and reuses the existing sink. That extension has its own AAPCS32
varargs/formatting contract and does not change this write-service ABI.

## Validation

The dedicated compatibility regression stages an ARM
`svc #0xa0; bx lr` stub, routes it through the feature-025 registry and
feature-024 dispatcher, verifies r0-r2 argument handling plus signed return
bits, and covers null tags, exact string limits, wrong service IDs, null text,
unreadable pointers, and unterminated over-limit strings.

Exact-head implementation revision
`59fa3eba1d53c6679ef6086cd209198ca7ecb4ac` PASSed:

- Linux A32 smoke check `108387154388`;
- Android x86_64 address-space probe check `108387154358`;
- Android arm64-v8a cross-build check `108387154270`.

These checks validate the service bridge and registered regression; they do not
establish a guest `liblog.so` shim, Android-device execution, or VLC/FMOD
compatibility.
