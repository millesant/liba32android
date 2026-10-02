# ELF32 linker strings

Status: bounded linker-string layer complete; historical feature 002 exact-head gate PASSed at CI #106

## Boundary

`elf32_linker_strings` sits above validated linker metadata and below `elf32_dependency_resolver`.

It consumes:

- `memory::GuestMemory` for read-only guest-byte access;
- an `Elf32StringTableMetadata` descriptor for single-entry reads;
- or a validated `Elf32LinkerMetadata` object for SONAME / ordered `DT_NEEDED` aggregation;
- an explicit caller-provided maximum payload length for every string operation.

It returns host-owned byte strings only. It does not expose guest pointers, access the filesystem, locate or load dependencies, normalize names, resolve symbols, apply relocations, modify mappings, or execute guest code.

```text
elf32_loader
    |
    v
elf32_dynamic
    |
    v
elf32_linker_metadata
    | validated STRTAB guest range
    | SONAME offset
    | ordered NEEDED offsets
    v
elf32_linker_strings
    | optional SONAME
    | ordered/repeated NEEDED byte strings
    v
elf32_dependency_resolver
    |
    v
requester-aware provider chain/catalogs -> dependency loader/link map
```

Guest addresses remain logical 32-bit values and all guest bytes are accessed through `GuestMemory`.

## Bounded single-entry reader

`read_elf32_string_table_entry` reads one NUL-terminated byte string from a STRTAB descriptor.

The caller must provide `max_string_bytes`; there is intentionally no implicit project-wide default in this slice.

The reader:

- rejects `offset >= STRSZ`;
- computes `guest_address + offset` with checked 64-bit arithmetic before narrowing;
- scans through fixed-size bounded reads rather than copying the whole string table;
- succeeds when NUL occurs with payload length `<= max_string_bytes`;
- reports `StringTooLong` after the first extra non-NUL payload byte beyond the configured ceiling;
- reports `UnterminatedString` when STRTAB ends without NUL before the length ceiling is exceeded;
- propagates guest read failures explicitly;
- accepts an empty string;
- preserves arbitrary non-NUL bytes without UTF-8 validation or normalization;
- never writes guest memory.

A payload exactly equal to the configured maximum is valid when the next byte is NUL.

## Aggregate SONAME / NEEDED semantics

`build_elf32_linker_strings` consumes the SONAME and NEEDED offsets already validated/collected by `elf32_linker_metadata`.

Behavior:

- no SONAME and no NEEDED entries succeeds with an empty result and does not require STRTAB;
- requested strings require a string-table descriptor;
- SONAME remains optional;
- NEEDED names preserve dynamic-array order;
- repeated offsets/names are preserved rather than deduplicated;
- offsets are defensively rechecked even though the normal metadata builder already validates them;
- aggregate success is all-or-nothing: if any requested string fails, the returned failed result does not expose a successful partial SONAME/NEEDED set.

The output uses `std::string` as an owned byte container. Encoding/path policy belongs to a later layer.

## Error model

The current error categories distinguish:

- missing required string table;
- string offset out of range;
- guest-address overflow;
- guest read failure;
- unterminated string;
- string exceeding the caller-selected payload ceiling.

Malformed input fails deterministically without guest mutation.

## Deliberate layer-local limits

These are `elf32_linker_strings` ownership boundaries, not a repository-wide list of missing functionality. Several downstream capabilities are implemented; genuinely deferred policy is identified explicitly.

This layer does **not**:

- open or load `DT_NEEDED` libraries itself (bounded acquisition/loading is implemented downstream);
- define Android search paths/namespaces or canonicalize path/name bytes (still deferred policy);
- own link-map/deduplication semantics (implemented downstream by the persistent dependency loader/link map);
- reject empty names itself; `elf32_dependency_resolver` applies that dependency-request policy;
- interpret SysV/GNU hash tables or dynamic symbol entries (implemented downstream by bounded symbol lookup);
- perform symbol lookup/interposition/versioning itself (implemented downstream for the accepted bounded scope);
- decode/apply ARM relocations or PLT/JMPREL itself (implemented downstream for the accepted REL/JUMP_SLOT set);
- process RELRO or lifecycle execution itself (implemented downstream); TLS, destructor/unload lifecycle, and Android packed relocations remain deferred.

Provider-backed bounded acquisition, requester-aware provider chaining/catalogs, recursive/persistent dependency loading, guest mapping, bounded symbol resolution, relocation, RELRO, and INIT_ARRAY execution now live in separate downstream layers. Android filesystem/namespace/search policy and unsupported TLS/packed/destructor semantics remain genuine gaps.

## Validation evidence

Synthetic coverage includes:

- ordinary and empty strings;
- arbitrary non-UTF-8 bytes;
- out-of-range offsets;
- checked address overflow;
- guest read failure;
- unterminated-at-STRTAB-end behavior;
- payload exactly at the caller ceiling followed by NUL;
- payload exceeding the ceiling;
- no-mutation failure behavior;
- optional SONAME;
- multiple and repeated ordered NEEDED names;
- no-strings/no-STRTAB success;
- missing STRTAB rejection;
- defensive aggregate offset rejection;
- later-NEEDED failure with no successful partial aggregate.

The pinned NDK-generated ARM32 fixture is loaded through the complete current linker metadata path, then consumed with an explicit 64-byte string ceiling. The integration case requires SONAME exactly `liba32android_loader_fixture.so` and zero NEEDED names.

GitHub Actions run `35402559596` (#102) passed the T003 implementation head. The Linux job executed `elf32_real_linker_strings` and reported 24/24 CTest cases passing; the Android `arm64-v8a` cross-build also passed. Historical feature task T005 then completed the exact-head gate at CI #106; linker-string work is complete. Later dependency/provider/link-map features are documented by their current architecture pages and exact-revision test/CI evidence.
