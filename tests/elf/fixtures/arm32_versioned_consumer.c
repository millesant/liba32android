// Reproducible freestanding ARMv7/Android consumer for feature-014.
#include <stdint.h>

__attribute__((visibility("default")))
uint32_t fixture_versioned_import(uint32_t value);

__attribute__((visibility("default"), noinline))
uint32_t fixture_versioned_call(uint32_t value) {
    return fixture_versioned_import(value) + 1U;
}
