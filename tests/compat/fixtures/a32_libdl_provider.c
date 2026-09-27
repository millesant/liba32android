__attribute__((visibility("default"), noinline))
int fixture_dynamic_value(void) {
    return 77;
}


__attribute__((visibility("default")))
volatile unsigned int fixture_dlclose_marker;

__attribute__((visibility("default")))
unsigned int fixture_dlclose_dso_handle = 0xA32D5000U;

__attribute__((noinline))
static void fixture_dlclose_fini(void) {
    fixture_dlclose_marker = 0x0D1C105EU;
}

__attribute__((section(".fini_array"), used))
static void (*const fixture_dlclose_fini_entry)(void) = fixture_dlclose_fini;
