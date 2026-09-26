# Design — partial ARM32 libc memory/string shim/provider

## Guest DSO

The shim source includes feature 030's preprocessor-safe service-ID header and
defines seven ARM functions. Each function executes only its service SVC and
bx lr.

The pinned-NDK builder uses -nostdlib, -ffreestanding, -fno-builtin,
-fno-stack-protector, ARM mode, no build-id, explicit 16 KiB max page size, and
SONAME libc.so.

## Consumer DSO

The consumer declares the seven standard signatures without including platform
headers and exports fixture_* wrappers. -fno-builtin prevents compiler folding
of memory/string calls. It links explicitly against the generated partial
libc.so.

## Provider path

The integration owns shim bytes and creates one
make_a32_libc_memory_string_shim_catalog_entry. Feature 031 serves that entry
through feature 029's app -> platform link exposing exact libc.so. An empty
application catalog remains first in the generic provider chain.

## Relocation

The root consumer must resolve each of the seven names from object 1, the shim.
After combined relocation, every resolved shim guest address must appear as the
final target of an R_ARM_JUMP_SLOT write. Extra unrelated writes are not
accepted as substitutes for any required function target.

## Execution

Map one bounded data page plus bounded stack pages. Construct one feature-030
service and register all seven SVC IDs to the same handler.

Look up each fixture wrapper from root object 0, derive ARM/Thumb state from its
symbol value, seed r0-r2, LR/stop/stack, and execute with one service-call
ceiling. Validate:

- memcpy memory and destination pointer;
- memset byte effects and destination pointer;
- memcmp negative sign;
- memchr logical guest pointer;
- strlen length;
- strcmp negative sign;
- strncmp equal-prefix zero.

## Evidence boundary

Passing fixture integration proves the partial shim/provider/service chain only.
It does not prove the complete libc dependency surface of any supplied target.
