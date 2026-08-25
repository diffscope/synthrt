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

# add_auto_test(<source> [extra link libraries...]
#     [RESOURCES <files or directories...>]
#     [RESOURCE_MACRO <macro>]
# )
#
# The target and the \c ctest case both take their name from \a source, so \c test_Foo.cpp yields
# \c test_Foo. The source is expected to define \c BOOST_TEST_MAIN before including the Boost.Test
# header, since it carries its own entry point.
#
# Resources are copied to <target-file-directory>/<target-name>_data. The target receives that
# directory through the \c TEST_RESOURCE_DIRECTORY compile definition. \c RESOURCE_MACRO replaces
# the default definition name when supplied.
#
# Expands to nothing when Boost is absent, so a caller needs no guard of its own.
function(add_auto_test _src)
    if(NOT Boost_FOUND)
        return()
    endif()

    set(options)
    set(oneValueArgs RESOURCE_MACRO)
    set(multiValueArgs RESOURCES)
    cmake_parse_arguments(FUNC "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    get_filename_component(_name ${_src} NAME_WE)
    add_executable(${_name} ${_src})
    target_link_libraries(${_name} PRIVATE Boost::unit_test_framework ${FUNC_UNPARSED_ARGUMENTS})

    if(FUNC_RESOURCES)
        set(_resource_macro TEST_RESOURCE_DIRECTORY)
        if(FUNC_RESOURCE_MACRO)
            set(_resource_macro ${FUNC_RESOURCE_MACRO})
        endif()

        set(_resource_directory ${_name}_data)
        qm_add_copy_command(${_name}
            SOURCES ${FUNC_RESOURCES}
            DESTINATION ${_resource_directory}
            SKIP_INSTALL
        )
        target_compile_definitions(${_name} PRIVATE
            ${_resource_macro}="$<TARGET_FILE_DIR:${_name}>/${_resource_directory}"
        )
    endif()

    add_test(NAME ${_name} COMMAND $<TARGET_FILE:${_name}>)
endfunction()
