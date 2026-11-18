# 0.0.2.0 renames typed contract export payloads from Schema to Exports.
project(synthrt-dsinfer
    LANGUAGES CXX
    VERSION 0.0.2.0
    DESCRIPTION "DiffSinger Inference Framework"
)

# ----------------------------------
# Build Options
# ----------------------------------
option(DSINFER_ENABLE_DIRECTML "Enable DirectML provider" ON)
option(DSINFER_ENABLE_CUDA "Enable CUDA provider" ON)
option(DSINFER_ENABLE_STATIC_PLUGINS "Enable static plugin linking" OFF) # TODO: Implement this option

# ----------------------------------
# Project Variables
# ----------------------------------
set(DSINFER_INSTALL ${SYNTHRT_INSTALL})
set(DSINFER_VERSION ${PROJECT_VERSION})
set(DSINFER_INSTALL_NAME ${PROJECT_NAME})
set(DSINFER_INSTALL_NAMESPACE synthrt)
# A plugin is loaded at run time and never linked, so it is a module library, which has no
# import library to install.
set(DSINFER_PLUGIN_TYPE MODULE)

include("conf.cmake")

dsinfer_init_buildsystem()

add_subdirectory(lib)

add_subdirectory(util)

add_subdirectory(plugins)

add_subdirectory(tools)

if(SYNTHRT_BUILD_TESTS)
    add_subdirectory(tests)
endif()

dsinfer_install()

dsinfer_finish_buildsystem()
