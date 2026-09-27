liba32android_add_test_executable(
    runtime_service_dispatch_test
    tests/runtime/a32_service_dispatch.cpp
)

add_test(
    NAME a32_host_service_dispatch
    COMMAND runtime_service_dispatch_test
)

liba32android_add_test_executable(
    runtime_service_registry_test
    tests/runtime/a32_service_registry.cpp
)

add_test(
    NAME a32_host_service_registry
    COMMAND runtime_service_registry_test
)

liba32android_add_test_executable(
    public_embedding_api_test
    tests/runtime/public_embedding_api.c
)
# Keep compilation in C while linking through the C++ driver because the
# shared runtime is implemented in C++ and carries its normal C++ runtime deps.
set_target_properties(
    public_embedding_api_test
    PROPERTIES LINKER_LANGUAGE CXX
)

add_test(
    NAME public_embedding_api
    COMMAND public_embedding_api_test
)
