cmake_minimum_required(VERSION 3.24)
project(liba32android LANGUAGES C CXX)

include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

function(liba32android_enable_android_16k_elf_alignment target)
    if(ANDROID)
        target_link_options(${target}
            PRIVATE
                "-Wl,-z,max-page-size=16384"
                "-Wl,-z,common-page-size=16384"
        )
    endif()
endfunction()

option(LIBA32ANDROID_BUILD_TESTS "Build liba32android tests" ${BUILD_TESTING})
option(LIBA32ANDROID_BUILD_BENCHMARKS "Build opt-in runtime benchmarks" OFF)
option(LIBA32ANDROID_BUILD_ANDROID_PROBES "Build Android-only diagnostic probes" OFF)
set(LIBA32ANDROID_ARM32_FIXTURE_PATH "" CACHE FILEPATH
    "Optional generated ARM32 Android ET_DYN fixture used by the real ELF32 loader integration test")
set(LIBA32ANDROID_ARM32_JUMP_SLOT_CONSUMER_PATH "" CACHE FILEPATH
    "Optional generated ARM32 Android JUMP_SLOT consumer DSO used by feature-009 integration")
set(LIBA32ANDROID_ARM32_JUMP_SLOT_PROVIDER_PATH "" CACHE FILEPATH
    "Optional generated ARM32 Android JUMP_SLOT provider DSO used by feature-009 integration")
set(LIBA32ANDROID_ARM32_VERSIONED_CONSUMER_PATH "" CACHE FILEPATH
    "Optional generated ARM32 Android versioned-symbol consumer DSO used by feature-014 integration")
set(LIBA32ANDROID_ARM32_VERSIONED_PROVIDER_PATH "" CACHE FILEPATH
    "Optional generated ARM32 Android versioned-symbol provider DSO used by feature-014 integration")
set(LIBA32ANDROID_ARM32_ANDROID_LOG_CONSUMER_PATH "" CACHE FILEPATH
    "Optional generated ARM32 Android liblog consumer DSO used by feature-027 integration")
set(LIBA32ANDROID_ARM32_ANDROID_LOG_SHIM_PATH "" CACHE FILEPATH
    "Optional generated ARM32 Android liblog.so compatibility shim used by feature-027 integration")
set(LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_CONSUMER_PATH "" CACHE FILEPATH
    "Optional generated ARM32 Android partial-libc consumer DSO used by feature-032 integration")
set(LIBA32ANDROID_ARM32_LIBC_MEMORY_STRING_SHIM_PATH "" CACHE FILEPATH
    "Optional generated ARM32 Android partial libc.so shim used by feature-032 integration")
set(LIBA32ANDROID_ARM32_PTHREAD_LIFECYCLE_CONSUMER_PATH "" CACHE FILEPATH
    "Optional generated ARM32 pthread lifecycle consumer DSO used by bounded lifecycle integration")
set(LIBA32ANDROID_ARM32_LIBDL_CONSUMER_PATH "" CACHE FILEPATH
    "Optional generated ARM32 Android libdl consumer DSO used by feature-046 integration")
set(LIBA32ANDROID_ARM32_LIBDL_SHIM_PATH "" CACHE FILEPATH
    "Optional generated ARM32 Android libdl.so compatibility shim used by feature-046 integration")
set(LIBA32ANDROID_ARM32_LIBDL_PROVIDER_PATH "" CACHE FILEPATH
    "Optional generated ARM32 Android resident libdl target DSO used by feature-046 integration")
set(LIBA32ANDROID_ARM32_LIBM_CONSUMER_PATH "" CACHE FILEPATH
    "Optional generated ARM32 Android libm consumer DSO used by feature-047 integration")
set(LIBA32ANDROID_ARM32_LIBM_SHIM_PATH "" CACHE FILEPATH
    "Optional generated ARM32 Android libm.so compatibility shim used by feature-047 integration")
set(LIBA32ANDROID_ARM32_APP_SEARCH_ROOT_PATH "" CACHE FILEPATH
    "Legacy optional raw ARM32 app-root fixture path retained for compatibility")
set(LIBA32ANDROID_ARM32_APP_SEARCH_APK_PATH "" CACHE FILEPATH
    "Optional deterministic APK containing ARM32 app-root/child DSOs for bootstrap integration")
set(LIBA32ANDROID_ARM32_JNI_ONLOAD_FIXTURE_PATH "" CACHE FILEPATH
    "Optional generated ARM32 JNI registration fixture DSO")

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LibA32AndroidDependencies.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LibA32AndroidRuntime.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LibA32AndroidBenchmarks.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LibA32AndroidAndroidProbes.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/LibA32AndroidTests.cmake)
