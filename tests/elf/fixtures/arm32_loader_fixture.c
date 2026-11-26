// Reproducible freestanding ARMv7/Android ELF32 fixture for loader-only tests.
// Built with the project's pinned Android NDK. It intentionally contains text,
// initialized data, and BSS while avoiding libc or Android runtime dependencies.

#include <stdint.h>

__attribute__((visibility("default"))) uint32_t fixture_data = 0x12345678U;
__attribute__((visibility("default"))) uint32_t fixture_bss;

__attribute__((visibility("default"), noinline))
uint32_t fixture_add(uint32_t lhs, uint32_t rhs) {
    return lhs + rhs + fixture_data + fixture_bss;
}
