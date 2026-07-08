if(NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 3 CONFIG REQUIRED)
endif()

include(Catch)

function(synthrt_add_unittest target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain)
    set_target_properties(${target} PROPERTIES FOLDER "Tests")
    catch_discover_tests(${target})
endfunction()

add_subdirectory(Core)
add_subdirectory(Driver)
add_subdirectory(S2P)
add_subdirectory(G2P)
add_subdirectory(C)
