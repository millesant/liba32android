if(NOT LIBA32ANDROID_BUILD_BENCHMARKS)
    return()
endif()

add_executable(
    a32_runtime_benchmark
    tools/benchmarks/a32_runtime_benchmark.cpp
)

target_include_directories(
    a32_runtime_benchmark
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    a32_runtime_benchmark
    PRIVATE
        liba32android
)

target_compile_features(a32_runtime_benchmark PRIVATE cxx_std_20)
