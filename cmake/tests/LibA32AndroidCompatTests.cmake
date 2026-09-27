liba32android_add_test_executable(
    compat_android_log_write_test
    tests/compat/a32_android_log_write.cpp
)

add_test(
    NAME a32_android_log_write_service
    COMMAND compat_android_log_write_test
)

liba32android_add_test_executable(
    compat_android_platform_provider_test
    tests/compat/a32_android_platform_provider.cpp
)

add_test(
    NAME a32_android_platform_provider
    COMMAND compat_android_platform_provider_test
)

liba32android_add_test_executable(
    compat_android_platform_catalog_provider_test
    tests/compat/a32_android_platform_catalog_provider.cpp
)

add_test(
    NAME a32_android_platform_catalog_provider
    COMMAND compat_android_platform_catalog_provider_test
)

liba32android_add_test_executable(
    compat_android_namespace_policy_test
    tests/compat/a32_android_namespace_policy.cpp
)

add_test(
    NAME a32_android_namespace_access_policy
    COMMAND compat_android_namespace_policy_test
)

liba32android_add_test_executable(
    compat_libc_memory_string_test
    tests/compat/a32_libc_memory_string.cpp
)

add_test(
    NAME a32_libc_memory_string_service
    COMMAND compat_libc_memory_string_test
)

liba32android_add_test_executable(
    compat_libc_string_copy_search_test
    tests/compat/a32_libc_string_copy_search.cpp
)

add_test(
    NAME a32_libc_string_copy_search_service
    COMMAND compat_libc_string_copy_search_test
)

liba32android_add_test_executable(
    compat_libc_integer_test
    tests/compat/a32_libc_integer.cpp
)

add_test(
    NAME a32_libc_integer_service
    COMMAND compat_libc_integer_test
)

liba32android_add_test_executable(
    compat_libc_heap_test
    tests/compat/a32_libc_heap.cpp
)

add_test(
    NAME a32_libc_heap_service
    COMMAND compat_libc_heap_test
)

liba32android_add_test_executable(
    compat_pthread_sync_test
    tests/compat/a32_pthread_sync.cpp
)

add_test(
    NAME a32_pthread_sync_service
    COMMAND compat_pthread_sync_test
)

liba32android_add_test_executable(
    compat_android_log_shim_integration_test
    tests/compat/a32_android_log_shim.cpp
)

if((LIBA32ANDROID_ARM32_ANDROID_LOG_CONSUMER_PATH AND
    NOT LIBA32ANDROID_ARM32_ANDROID_LOG_SHIM_PATH) OR
   (LIBA32ANDROID_ARM32_ANDROID_LOG_SHIM_PATH AND
    NOT LIBA32ANDROID_ARM32_ANDROID_LOG_CONSUMER_PATH))
    message(FATAL_ERROR
        "Both LIBA32ANDROID_ARM32_ANDROID_LOG_CONSUMER_PATH and LIBA32ANDROID_ARM32_ANDROID_LOG_SHIM_PATH must be supplied together")
endif()

if(LIBA32ANDROID_ARM32_ANDROID_LOG_CONSUMER_PATH AND
   LIBA32ANDROID_ARM32_ANDROID_LOG_SHIM_PATH)
    if(NOT EXISTS "${LIBA32ANDROID_ARM32_ANDROID_LOG_CONSUMER_PATH}")
        message(FATAL_ERROR
            "Android log consumer fixture does not exist: ${LIBA32ANDROID_ARM32_ANDROID_LOG_CONSUMER_PATH}")
    endif()
    if(NOT EXISTS "${LIBA32ANDROID_ARM32_ANDROID_LOG_SHIM_PATH}")
        message(FATAL_ERROR
            "Android log shim fixture does not exist: ${LIBA32ANDROID_ARM32_ANDROID_LOG_SHIM_PATH}")
    endif()
    add_test(
        NAME a32_android_log_shim_integration
        COMMAND compat_android_log_shim_integration_test
                "${LIBA32ANDROID_ARM32_ANDROID_LOG_CONSUMER_PATH}"
                "${LIBA32ANDROID_ARM32_ANDROID_LOG_SHIM_PATH}"
    )
endif()

liba32android_add_test_executable(
    compat_libc_memory_string_shim_integration_test
    tests/compat/a32_libc_memory_string_shim.cpp
)

if((LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_CONSUMER_PATH AND
    NOT LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_SHIM_PATH) OR
   (LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_SHIM_PATH AND
    NOT LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_CONSUMER_PATH))
    message(FATAL_ERROR
        "Both LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_CONSUMER_PATH and LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_SHIM_PATH must be supplied together")
endif()

if(LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_CONSUMER_PATH AND
   LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_SHIM_PATH)
    if(NOT EXISTS "${LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_CONSUMER_PATH}")
        message(FATAL_ERROR
            "libc memory/string consumer fixture does not exist: ${LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_CONSUMER_PATH}")
    endif()
    if(NOT EXISTS "${LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_SHIM_PATH}")
        message(FATAL_ERROR
            "libc memory/string shim fixture does not exist: ${LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_SHIM_PATH}")
    endif()
    add_test(
        NAME a32_libc_memory_string_shim_integration
        COMMAND compat_libc_memory_string_shim_integration_test
                "${LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_CONSUMER_PATH}"
                "${LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_SHIM_PATH}"
    )
endif()

liba32android_add_test_executable(
    compat_libdl_test
    tests/compat/a32_libdl.cpp
)

add_test(
    NAME a32_libdl_service
    COMMAND compat_libdl_test
)

liba32android_add_test_executable(
    compat_libdl_shim_integration_test
    tests/compat/a32_libdl_shim.cpp
)

if((LIBA32ANDROID_ARM32_LIBDL_CONSUMER_PATH OR
    LIBA32ANDROID_ARM32_LIBDL_SHIM_PATH OR
    LIBA32ANDROID_ARM32_LIBDL_PROVIDER_PATH) AND
   NOT (LIBA32ANDROID_ARM32_LIBDL_CONSUMER_PATH AND
        LIBA32ANDROID_ARM32_LIBDL_SHIM_PATH AND
        LIBA32ANDROID_ARM32_LIBDL_PROVIDER_PATH))
    message(FATAL_ERROR
        "LIBA32ANDROID_ARM32_LIBDL_CONSUMER_PATH, LIBA32ANDROID_ARM32_LIBDL_SHIM_PATH, and LIBA32ANDROID_ARM32_LIBDL_PROVIDER_PATH must be supplied together")
endif()

if(LIBA32ANDROID_ARM32_LIBDL_CONSUMER_PATH AND
   LIBA32ANDROID_ARM32_LIBDL_SHIM_PATH AND
   LIBA32ANDROID_ARM32_LIBDL_PROVIDER_PATH)
    if(NOT EXISTS "${LIBA32ANDROID_ARM32_LIBDL_CONSUMER_PATH}")
        message(FATAL_ERROR
            "libdl consumer fixture does not exist: ${LIBA32ANDROID_ARM32_LIBDL_CONSUMER_PATH}")
    endif()
    if(NOT EXISTS "${LIBA32ANDROID_ARM32_LIBDL_SHIM_PATH}")
        message(FATAL_ERROR
            "libdl shim fixture does not exist: ${LIBA32ANDROID_ARM32_LIBDL_SHIM_PATH}")
    endif()
    if(NOT EXISTS "${LIBA32ANDROID_ARM32_LIBDL_PROVIDER_PATH}")
        message(FATAL_ERROR
            "libdl provider fixture does not exist: ${LIBA32ANDROID_ARM32_LIBDL_PROVIDER_PATH}")
    endif()
    add_test(
        NAME a32_libdl_shim_integration
        COMMAND compat_libdl_shim_integration_test
                "${LIBA32ANDROID_ARM32_LIBDL_CONSUMER_PATH}"
                "${LIBA32ANDROID_ARM32_LIBDL_SHIM_PATH}"
                "${LIBA32ANDROID_ARM32_LIBDL_PROVIDER_PATH}"
    )
endif()
