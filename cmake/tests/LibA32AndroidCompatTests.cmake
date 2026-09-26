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
    compat_android_namespace_policy_test
    tests/compat/a32_android_namespace_policy.cpp
)

add_test(
    NAME a32_android_namespace_access_policy
    COMMAND compat_android_namespace_policy_test
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
