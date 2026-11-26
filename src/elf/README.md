# ELF implementation layout

Public/internal repository-facing ELF contracts remain as headers directly under `src/elf/`. Implementations are grouped by layer:

- `loading/`: load-plan validation, ET_DYN placement, and mapped loading;
- `metadata/`: structural dynamic entries plus linker metadata/string materialization;
- `linking/`: dependency graph loading, symbol lookup, and relocation;
- `hardening/`: post-relocation GNU RELRO sealing;
- `internal/`: helpers shared across implementations but not intended as layer contracts.

The dependency direction remains one-way. Loading must not absorb dynamic-linker semantics; structural metadata must not mutate guest memory; linker stages operate on logical guest addresses; hardening occurs only after relocation writes are complete.
