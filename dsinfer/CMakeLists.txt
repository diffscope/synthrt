project(dsinfer)

add_subdirectory(lib)

if(SYNTHRT_BUILD_TESTS)
    add_subdirectory(tests)
endif()