// Reproducible freestanding ARMv7/Android consumer for feature-009
// JUMP_SLOT integration. Linking against the provider DSO must produce a
// DT_NEEDED edge and an R_ARM_JUMP_SLOT relocation for fixture_import.

#include <stdint.h>

__attribute__((visibility("default")))
uint32_t fixture_import(uint32_t value);

__attribute__((visibility("default"), noinline))
uint32_t fixture_call_import(uint32_t value) {
    return fixture_import(value) + 1U;
}
