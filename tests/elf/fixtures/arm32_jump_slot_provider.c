// Reproducible freestanding ARMv7/Android provider for feature-009
// JUMP_SLOT integration. Built with the project's pinned Android NDK.

#include <stdint.h>

__attribute__((visibility("default"), noinline))
uint32_t fixture_import(uint32_t value) {
    return value + 7U;
}
