cmake_minimum_required(VERSION 3.16)

project(synthrt
    VERSION 0.0.1.1
    LANGUAGES CXX
    DESCRIPTION "SVS Synthesis Runtime"
    HOMEPAGE_URL "https://github.com/diffscope/synthrt"
)

# ----------------------------------
# Build Options
# ----------------------------------
option(SYNTHRT_BUILD_STATIC "Build static library" OFF)
option(SYNTHRT_BUILD_TESTS "Build test cases" OFF)
option(SYNTHRT_BUILD_EXAMPLES "Build examples" OFF)
option(SYNTHRT_INSTALL "Install library" ON)

# ----------------------------------
# CMake Settings
# ----------------------------------
if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
endif()

if(NOT DEFINED CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
endif()

if(NOT DEFINED CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
endif()

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

add_subdirectory(third-party)
add_subdirectory(synthrt)
add_subdirectory(dsinfer)