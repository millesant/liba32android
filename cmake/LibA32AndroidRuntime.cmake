add_library(liba32android SHARED
    src/public/liba32android_c.cpp
    src/cpu/dynarmic_cpu.cpp
    src/runtime/a32_service_dispatch.cpp
    src/runtime/a32_logical_thread.cpp
    src/runtime/a32_service_registry.cpp
    src/compat/a32_android_log_write.cpp
    src/compat/a32_android_platform_provider.cpp
    src/compat/a32_android_namespace_policy.cpp
    src/compat/a32_libc_memory_string.cpp
    src/compat/a32_libc_integer.cpp
    src/compat/a32_libc_clock.cpp
    src/compat/a32_libc_heap.cpp
    src/compat/a32_pthread_sync.cpp
    src/compat/a32_pthread_lifecycle.cpp
    src/compat/a32_signal.cpp
    src/compat/a32_scheduler.cpp
    src/compat/a32_libdl.cpp
    src/compat/a32_libdl_close_transaction.cpp
    src/compat/a32_libdl_open_transaction.cpp
    src/compat/a32_libdl_unload_transaction.cpp
    src/compat/a32_libm.cpp
    src/compat/a32_aeabi_atexit.cpp
    src/compat/a32_android_library_search.cpp
    src/compat/a32_android_apk_library_source.cpp
    src/compat/a32_android_apk_runtime.cpp
    src/compat/a32_jni.cpp
    src/elf/loading/elf32_dynamic_placement.cpp
    src/elf/loading/elf32_load_plan.cpp
    src/elf/loading/elf32_loader.cpp
    src/elf/metadata/elf32_dynamic.cpp
    src/elf/metadata/elf32_linker_metadata.cpp
    src/elf/metadata/elf32_linker_strings.cpp
    src/elf/linking/elf32_dependency_loader.cpp
    src/elf/linking/elf32_link_map_reclamation.cpp
    src/elf/linking/elf32_dependency_resolver.cpp
    src/elf/linking/elf32_lifecycle.cpp
    src/elf/linking/elf32_relocation.cpp
    src/elf/linking/elf32_symbol_lookup.cpp
    src/elf/linking/elf32_symbol_versioning.cpp
    src/elf/hardening/elf32_relro.cpp
    src/memory/guest_memory.cpp
    src/memory/guest_va_allocator.cpp
)

# CMake automatically prefixes shared libraries with "lib" on Android/Linux.
# Keep the internal target name stable while producing exactly liba32android.so.
set_target_properties(liba32android PROPERTIES OUTPUT_NAME a32android)

target_include_directories(liba32android
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(liba32android
    PRIVATE
        dynarmic
        ${LIBA32ANDROID_ZLIB_TARGET}
        m
)

target_compile_features(liba32android PRIVATE cxx_std_20)
liba32android_enable_android_16k_elf_alignment(liba32android)

install(
    TARGETS liba32android
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/liba32android
    DESTINATION include
)
