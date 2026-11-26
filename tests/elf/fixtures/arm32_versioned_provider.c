// Reproducible freestanding ARMv7/Android provider for feature-014.
#include <stdint.h>

__attribute__((visibility("default"), noinline))
uint32_t fixture_versioned_import(uint32_t value) {
    return value + 11U;
}
