cmake_minimum_required(VERSION 3.19)

project(synthrt
    VERSION 0.0.2.5
    LANGUAGES CXX
    DESCRIPTION "SVS Synthesis Runtime Library"
    HOMEPAGE_URL "https://github.com/diffscope/synthrt"
)

# ----------------------------------
# Build Options
# ----------------------------------
option(SYNTHRT_BUILD_TESTS "Build test cases" OFF)
option(SYNTHRT_BUILD_EXAMPLES "Build examples" OFF)
option(SYNTHRT_INSTALL "Install library" ON)
option(SYNTHRT_INSTALL_DEPLOY "Install with deploying dependencies" OFF)
option(SYNTHRT_INSTALL_MSVC_RUNTIME "Install MSVC runtime" OFF)

# ----------------------------------
# CMake Settings
# ----------------------------------
if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /manifest:no")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /manifest:no")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /manifest:no")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")

    if(NOT DEFINED CMAKE_DEBUG_POSTFIX)
        set(CMAKE_DEBUG_POSTFIX "d")
    endif()
elseif(MINGW)
    set(CMAKE_STATIC_LIBRARY_PREFIX "")
    set(CMAKE_SHARED_LIBRARY_PREFIX "")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
endif()

if(SYNTHRT_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
endif()

# ----------------------------------
# Project Variables
# ----------------------------------
set(SYNTHRT_VERSION ${PROJECT_VERSION})
set(SYNTHRT_INSTALL_NAME ${PROJECT_NAME})
set(SYNTHRT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# ----------------------------------
# Find basic dependencies
# ----------------------------------
find_package(qmsetup REQUIRED)

qm_import(Filesystem)
qm_init_directories()

# ----------------------------------
# Add source modules
# ----------------------------------
add_subdirectory(third-party)
add_subdirectory(synthrt)
add_subdirectory(dsinfer)