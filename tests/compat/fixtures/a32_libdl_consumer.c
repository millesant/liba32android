// Freestanding ARMv7/Android consumer for the feature-046 libdl shim.

__attribute__((visibility("default")))
void* dlopen(const char* filename, int flags);
__attribute__((visibility("default")))
void* dlsym(void* handle, const char* symbol);
__attribute__((visibility("default")))
int dlclose(void* handle);
__attribute__((visibility("default")))
char* dlerror(void);
__attribute__((visibility("default")))
int dladdr(const void* address, void* info);

__attribute__((visibility("default")))
int fixture_dynamic_value(void);

__attribute__((visibility("default"), noinline))
void* fixture_dlopen(const char* filename, int flags) {
    return dlopen(filename, flags);
}

__attribute__((visibility("default"), noinline))
void* fixture_dlsym(void* handle, const char* symbol) {
    return dlsym(handle, symbol);
}

__attribute__((visibility("default"), noinline))
int fixture_dlclose(void* handle) {
    return dlclose(handle);
}

__attribute__((visibility("default"), noinline))
char* fixture_dlerror(void) {
    return dlerror();
}

__attribute__((visibility("default"), noinline))
int fixture_dladdr(const void* address, void* info) {
    return dladdr(address, info);
}

__attribute__((visibility("default"), noinline))
int fixture_direct_provider_call(void) {
    return fixture_dynamic_value();
}
