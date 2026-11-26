if(LIBA32ANDROID_BUILD_TESTS)
    function(liba32android_add_test_executable target source)
        add_executable(${target} ${source})
        target_include_directories(${target}
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/elf
        )
        target_link_libraries(${target} PRIVATE liba32android)
    endfunction()

    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/LibA32AndroidCpuTests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/LibA32AndroidRuntimeTests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/LibA32AndroidCompatTests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/LibA32AndroidMemoryTests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/LibA32AndroidElfTests.cmake)
endif()
