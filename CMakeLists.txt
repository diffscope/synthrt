 cmake_minimum_required(VERSION 3.19)

project(synthrt
    VERSION 0.0.2.5
    LANGUAGES CXX
    DESCRIPTION "SVS Synthesis Runtime Library"
    HOMEPAGE_URL "https://github.com/diffscope/synthrt"
)

# ----------------------------------
# C++ Standard
# ----------------------------------
set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----------------------------------
# Build Options
# ----------------------------------
option(SYNTHRT_BUILD_TESTS "Build test cases" OFF)
option(SYNTHRT_BUILD_EXAMPLES "Build examples" OFF)
option(SYNTHRT_INSTALL "Install library" ON)
option(SYNTHRT_INSTALL_DEPLOY "Install with deploying dependencies" OFF)
option(SYNTHRT_INSTALL_MSVC_RUNTIME "Install MSVC runtime" OFF)

if(NOT DEFINED BUILD_TESTING)
    set(BUILD_TESTING ${SYNTHRT_BUILD_TESTS} CACHE BOOL "Build tests")
endif()
include(CTest)
if(BUILD_TESTING AND NOT SYNTHRT_BUILD_TESTS)
    set(SYNTHRT_BUILD_TESTS ON CACHE BOOL "Build test cases" FORCE)
endif()
if(SYNTHRT_BUILD_TESTS AND NOT BUILD_TESTING)
    set(BUILD_TESTING ON CACHE BOOL "Build tests" FORCE)
endif()

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

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ----------------------------------
# Add source modules
# ----------------------------------
set(CMAKE_FOLDER "ThirdParty")
add_subdirectory(third-party)

# Generic runtime (dependency bottom-up)
set(CMAKE_FOLDER "Libraries")
add_subdirectory(lib/Core)
add_subdirectory(lib/Audio)
add_subdirectory(lib/Driver)
add_subdirectory(lib/Extract)
add_subdirectory(lib/S2P)
# v2 Phase 5: SVS extracted from lib/Core/SVS/ as independent srt-svs target
# (reverses v1 Phase 2 方案 A). Must come after lib/Core.
add_subdirectory(lib/SVS)

# DiffSinger bank domain (moved before G2P: LanguageService depends on ds::bank)
set(CMAKE_FOLDER "Domains")
add_subdirectory(domains/ds-bank)

# G2P (depends on ds-bank for LanguageService, v2 Phase 1)
set(CMAKE_FOLDER "Libraries")
add_subdirectory(lib/G2P)

# DiffSinger inference domain
set(CMAKE_FOLDER "Domains")
add_subdirectory(domains/ds-infer)

# C ABI (composes VoicebankScanner + LanguageService + Runtime)
set(CMAKE_FOLDER "Libraries")
add_subdirectory(lib/C)

# Plugins
set(CMAKE_FOLDER "Plugins")
add_subdirectory(plugins)

# Tools
set(CMAKE_FOLDER "Tools")
add_subdirectory(tools)

# Aggregate target: synthrt::synthrt — links all core runtime libraries
if(NOT TARGET synthrt-synthrt)
    add_library(synthrt-synthrt INTERFACE)
    add_library(synthrt::synthrt ALIAS synthrt-synthrt)
    target_link_libraries(synthrt-synthrt INTERFACE
        srt::core srt::driver srt::s2p srt::svs srt-ds::bank)
    set_target_properties(synthrt-synthrt PROPERTIES FOLDER "Domains")
endif()

# Domain-specific aggregate: srt::diffsinger — adds G2P and ds-infer on top
if(NOT TARGET srt-diffsinger)
    add_library(srt-diffsinger INTERFACE)
    add_library(srt::diffsinger ALIAS srt-diffsinger)
    target_link_libraries(srt-diffsinger INTERFACE
        srt::g2p srt-ds::bank srt-ds::infer)
    set_target_properties(srt-diffsinger PROPERTIES FOLDER "Domains")
endif()

# ----------------------------------
# Export aggregate targets for find_package(synthrt)
# ----------------------------------
if(SYNTHRT_INSTALL)
    set_target_properties(synthrt-synthrt PROPERTIES EXPORT_NAME synthrt)
    set_target_properties(srt-diffsinger PROPERTIES EXPORT_NAME diffsinger)
    install(TARGETS synthrt-synthrt srt-diffsinger
        EXPORT synthrtTargets
    )
    install(EXPORT synthrtTargets
        NAMESPACE srt::
        FILE synthrtTargets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/synthrt
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/synthrtConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/synthrtConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/synthrt
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/synthrtConfigVersion.cmake"
        VERSION ${SYNTHRT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/synthrtConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/synthrtConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/synthrt
    )

    # Install official G2P packages (Phonetic-Suite-*) required by LanguageService
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/common/G2pPackages")
        install(DIRECTORY tests/common/G2pPackages
            DESTINATION ${CMAKE_INSTALL_DATADIR}/synthrt
        )
    endif()
endif()

if(SYNTHRT_BUILD_TESTS)
    set(CMAKE_FOLDER "Tests")
    add_subdirectory(unittests)
    add_subdirectory(tests)
endif()
