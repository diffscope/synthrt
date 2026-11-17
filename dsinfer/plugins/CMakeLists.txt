find_package(stdcorelib-plugin CONFIG REQUIRED)

# The plugins combine dsinfer_add_plugin() with the building blocks of AddPlugin.cmake, which
# describes the layout and the include namespace.
include("${SYNTHRT_SOURCE_DIR}/cmake/AddPlugin.cmake")

# Declares an interpreter plugin of this repository from the directory that contains it:
#
#   dsinfer_add_interpreter_plugin(<target> [LINKS <libs>...] [LINKS_PRIVATE <libs>...])
#
# The directory name is the name of the bundle within its category and the include namespace of
# the plugin. The sources but main.cpp are compiled into the object library <target>_objects that
# the tests link, and plugin.json is configured beside the library. Every plugin links dsinfer.
# LINKS adds the dependencies that the headers of the plugin use, and LINKS_PRIVATE those that only
# its sources use.
#
# The CMakeLists.txt of the category directory sets CURRENT_PLUGIN_CATEGORY and CURRENT_PLUGIN_IID.
function(dsinfer_add_interpreter_plugin _target)
    cmake_parse_arguments(FUNC "" "" "LINKS;LINKS_PRIVATE" ${ARGN})
    get_filename_component(_name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)

    synthrt_add_plugin_objects(${_target}
        FEATURES cxx_std_17
        LINKS synthrt-dsinfer ${FUNC_LINKS}
        LINKS_PRIVATE ${FUNC_LINKS_PRIVATE}
        INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/..
        INCLUDE_PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/*
    )
    dsinfer_add_plugin(${_target} CATEGORY ${CURRENT_PLUGIN_CATEGORY}/${_name}
        SOURCES main.cpp
        FEATURES cxx_std_17
        LINKS_PRIVATE ${_target}_objects ${FUNC_LINKS_PRIVATE}
    )
    stdc_add_plugin_metadata(TARGET ${_target} IID ${CURRENT_PLUGIN_IID})
    synthrt_configure_plugin_manifest(${_target} dsinfer)
endfunction()

add_subdirectory(singerproviders)

add_subdirectory(inferencedrivers)

add_subdirectory(inferenceinterpreters)
