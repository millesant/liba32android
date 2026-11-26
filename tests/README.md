# Tests

Tests are grouped by subsystem and evidence type.

- `cpu/`: CPU execution and fastmem behavior.
- `memory/`: guest-memory and mapped-address-space behavior.
- `elf/unit/`: synthetic ELF/linker cases with tightly controlled inputs.
- `elf/integration/`: generated real ARM32 ELF fixture integration.
- `elf/fixtures/`: source inputs for reproducible fixture generation.
- `elf/support/`: shared test-only helpers.

CMake registration lives under `cmake/tests/`. Keep target and CTest names stable so CI/evidence references remain meaningful across source-tree cleanup.
