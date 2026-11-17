# Building blocks for the plugins of a library built on synthrt.
#
# A repository declares a plugin with <helpers>_add_plugin() of its qmsetup repo helpers, which
# builds the library into <HELPERS>_BUILD_PLUGINS_DIR/<category> and installs it, and calls the
# functions below beside it for the parts that the helper does not provide. A repository usually
# combines the calls in a function of its own, such as wolf_add_interpreter_plugin().
#
#   synthrt_add_plugin_objects()         the object library of the plugin, which tests link
#   stdc_add_plugin_metadata()           the IID embedded in the plugin, from stdcorelib-plugin
#   synthrt_configure_plugin_manifest()  the plugin.json of the bundle, built and installed
#
# A plugin lives in a directory of its own under the directory of its category, with its headers
# and its sources side by side rather than split into include and src. The name of that directory
# is the include namespace of the plugin: the caller gives the object library the category
# directory as its include root, so a test, or one header of the plugin including another, writes
# <onnxdriver/Session/Session.h> in the way a host writes <dsinfer/Support/ErrorCode.h>. The
# namespace must therefore be unique among the plugins of a build, across categories as well, and
# a repeated one stops the configuration.
#
# The library keeps the debug postfix of the build. The bundle loader of stdcorelib-plugin tries
# the name field of plugin.json with the postfix d first in a debug build of MSVC, the only build
# in which the repositories set a postfix, and then the name alone. A multi-config generator is
# not supported, because it builds the library into a subdirectory for the configuration, away
# from plugin.json.

include_guard(GLOBAL)

# synthrt_add_plugin_objects(<target>
#     [DIRECTORY <directory>]
#     [<arguments of qm_configure_target()>...]
# )
#
# Creates the object library <target>_objects of the plugin <target> and configures it with
# qm_configure_target(), to which every argument but DIRECTORY is forwarded unchanged. The plugin
# links those objects, and a test can link them as well and reach the internals of the plugin
# without loading it.
#
# The directory of the plugin is the current source directory, or DIRECTORY relative to it, for a
# repository that declares the plugins of a category in the CMakeLists.txt of the category. If the
# arguments name no SOURCES, the sources are every header and source under that directory but
# main.cpp. The entry main.cpp is kept out of the object library because it exports the plugin
# instance, and two plugins linked into one test would otherwise export it twice.
#
# The usage requirements are the caller's: the include directories, the compile features, the
# public dependencies and the definitions, which the entry, the plugin and every test that links
# the objects compile with. A dependency that only the sources use is linked privately, so a test
# that links the objects links it too but compiles without its usage requirements, as with a static
# library. The plugin links such a dependency itself as well, so that the entry compiles under the
# requirements of the objects it is linked with:
#
#   synthrt_add_plugin_objects(acoustic
#       FEATURES cxx_std_17
#       LINKS synthrt-dsinfer
#       LINKS_PRIVATE inferutil
#       INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/..
#       INCLUDE_PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/*
#   )
#   dsinfer_add_plugin(acoustic CATEGORY inferenceinterpreters/acoustic NO_EXPORT
#       SOURCES main.cpp
#       FEATURES cxx_std_17
#       LINKS_PRIVATE acoustic_objects inferutil
#   )
#
# The headers of a plugin are not installed, so nothing outside the repository sees them.
function(synthrt_add_plugin_objects _target)
    cmake_parse_arguments(FUNC "" "DIRECTORY" "" ${ARGN})

    set(_dir "${CMAKE_CURRENT_SOURCE_DIR}")
    if(FUNC_DIRECTORY)
        get_filename_component(_dir "${FUNC_DIRECTORY}" ABSOLUTE)
    endif()
    get_filename_component(_namespace "${_dir}" NAME)

    # One namespace per plugin of a build, since the include roots of all its categories can meet
    # in one test.
    get_property(_owner GLOBAL PROPERTY "_SYNTHRT_PLUGIN_NAMESPACE_${_namespace}")
    if(_owner)
        message(FATAL_ERROR "the include namespace ${_namespace} of ${_dir} is already the "
                            "namespace of ${_owner}; give one of the plugins another directory name")
    endif()
    set_property(GLOBAL PROPERTY "_SYNTHRT_PLUGIN_NAMESPACE_${_namespace}" "${_dir}")

    # SOURCES is only looked for, not parsed: a multi-value keyword of this function would take the
    # keywords of qm_configure_target() that follow it as file names.
    set(_sources)
    if(NOT "SOURCES" IN_LIST FUNC_UNPARSED_ARGUMENTS)
        file(GLOB_RECURSE _sources CONFIGURE_DEPENDS "${_dir}/*.h" "${_dir}/*.cpp")
        list(REMOVE_ITEM _sources "${_dir}/main.cpp")
        set(_sources SOURCES ${_sources})
    endif()

    add_library(${_target}_objects OBJECT)
    qm_configure_target(${_target}_objects ${_sources} ${FUNC_UNPARSED_ARGUMENTS})
endfunction()

# synthrt_configure_plugin_manifest(<target> <helpers> [FILE <file>] [NO_INSTALL])
#
# Configures the plugin.json of the plugin <target> into the bundle of the build tree, the output
# directory of the library, and installs it beside the library. <helpers> is the function prefix of
# the qmsetup repo helpers whose <helpers>_add_plugin() declared the plugin, and whose upper-case
# form is the prefix of the install variables. FILE is the manifest, plugin.json of the current
# source directory if absent. NO_INSTALL omits the install rule, as NO_INSTALL of the helper does.
#
# The manifest is configured rather than copied, so that a changed manifest is refreshed and a
# deleted one restored: discovery resolves the library through its name field, and a stale
# manifest makes the plugin undiscoverable.
function(synthrt_configure_plugin_manifest _target _helpers)
    set(options NO_INSTALL)
    set(oneValueArgs FILE)
    set(multiValueArgs)
    cmake_parse_arguments(FUNC "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    string(TOUPPER "${_helpers}" _vars)
    set(_manifest "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json")
    if(FUNC_FILE)
        get_filename_component(_manifest "${FUNC_FILE}" ABSOLUTE)
    endif()

    get_target_property(_bundle_dir ${_target} LIBRARY_OUTPUT_DIRECTORY)
    configure_file("${_manifest}" "${_bundle_dir}/plugin.json" COPYONLY)

    if(${_vars}_INSTALL AND NOT FUNC_NO_INSTALL)
        get_target_property(_category ${_target} ${_vars}_PLUGIN_CATEGORY)
        install(FILES "${_manifest}"
            DESTINATION "${${_vars}_INSTALL_PLUGINS_DIR}/${_category}"
            RENAME plugin.json
        )
    endif()
endfunction()
