# Boost.Test based automatic tests.
#
# One executable per test source, rather than a single binary linking every case. A crash, a hang or
# a static initialization failure then isolates to the class under test instead of taking the whole
# suite down with it, and \c ctest can run the cases in parallel.

set(Boost_USE_STATIC_LIBS OFF)
find_package(Boost 1.67.0 QUIET COMPONENTS unit_test_framework CONFIG)

if(NOT Boost_FOUND)
    message(WARNING "Boost not found, auto tests will not be built")
endif()

# add_auto_test(<source> [extra link libraries...])
#
# The target and the \c ctest case both take their name from \a source, so \c test_Foo.cpp yields
# \c test_Foo. The source is expected to define \c BOOST_TEST_MAIN before including the Boost.Test
# header, since it carries its own entry point.
#
# Expands to nothing when Boost is absent, so a caller needs no guard of its own.
function(add_auto_test _src)
    if(NOT Boost_FOUND)
        return()
    endif()

    get_filename_component(_name ${_src} NAME_WE)
    add_executable(${_name} ${_src})
    target_link_libraries(${_name} PRIVATE Boost::unit_test_framework ${ARGN})
    add_test(NAME ${_name} COMMAND $<TARGET_FILE:${_name}>)
endfunction()
