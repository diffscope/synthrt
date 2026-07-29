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
include(CMakeDependentOption)

option(SYNTHRT_BUILD_TESTS "Build test cases" OFF)
# NOTE: All tests are unified under SYNTHRT_BUILD_TESTS. No separate L2 level.
option(SYNTHRT_BUILD_EXAMPLES "Build examples" OFF)
option(SYNTHRT_BUILD_TOOLS "Build command line tools" ON)
option(SYNTHRT_INSTALL "Install library" ON)
option(SYNTHRT_ENABLE_LUAJIT "Enable LuaJIT-backed S2P support" ON)
option(SYNTHRT_ENABLE_XSIMD "Enable xsimd audio acceleration" OFF)
cmake_dependent_option(SYNTHRT_INSTALL_TOOLS "Install command line tools" ON
    "SYNTHRT_INSTALL;SYNTHRT_BUILD_TOOLS" OFF)
option(SYNTHRT_INSTALL_DEPLOY "Install with deploying dependencies" OFF)
option(SYNTHRT_INSTALL_MSVC_RUNTIME "Install MSVC runtime" OFF)
option(SYNTHRT_ENABLE_CLANG_TIDY "Enable clang-tidy warnings (warn-only)" OFF)
if(SYNTHRT_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy-16)
    if(CLANG_TIDY_EXE)
        message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--warnings-as-errors=")
    else()
        message(WARNING "clang-tidy not found but SYNTHRT_ENABLE_CLANG_TIDY=ON")
    endif()
endif()

option(SYNTHRT_ENABLE_COVERAGE "Enable code coverage" OFF)
if(SYNTHRT_ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(WARNING "Coverage only supported on GCC, ignoring SYNTHRT_ENABLE_COVERAGE")
    else()
        add_compile_options(--coverage -O0 -g)
        link_libraries(--coverage)
    endif()
endif()

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

    # RC compiler (resource) needs system include paths to find windows.h.
    # CMake's cmcldeps wrapper invokes rc.exe without forwarding the INCLUDE
    # environment variable (which is normally set by vcvarsall.bat), so
    # #include <windows.h> in .rc files fails with C1083.
    # Workaround: derive MSVC + Windows SDK include paths from the CXX
    # compiler location and add them as -I flags to CMAKE_RC_FLAGS.
    if(NOT DEFINED _SYNTHRT_RC_FLAGS_INITIALIZED)
        set(_rc_extra_flags "")
        # MSVC STL headers (e.g. <string>, <memory>): derive from cl.exe path
        # cl.exe at: <MSVC_ROOT>/bin/Hostx64/x64/cl.exe
        # include at: <MSVC_ROOT>/include
        get_filename_component(_msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_msvc_tools_root "${_msvc_bin_dir}/../../.." ABSOLUTE)
        if(EXISTS "${_msvc_tools_root}/include")
            string(APPEND _rc_extra_flags " -I\"${_msvc_tools_root}/include\"")
        endif()

        # Windows SDK headers (windows.h, winres.h, etc.)
        # Look under C:/Program Files (x86)/Windows Kits/10/Include/*/um
        file(GLOB _win_sdk_inc_dirs
            "C:/Program Files (x86)/Windows Kits/10/Include/*/um"
            "C:/Program Files (x86)/Windows Kits/10/Include/*/shared"
            "C:/Program Files (x86)/Windows Kits/10/Include/*/ucrt")
        foreach(_dir ${_win_sdk_inc_dirs})
            string(APPEND _rc_extra_flags " -I\"${_dir}\"")
        endforeach()

        if(_rc_extra_flags)
            set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS}${_rc_extra_flags}" CACHE STRING "RC flags (with system include paths)" FORCE)
            message(STATUS "CMAKE_RC_FLAGS augmented with system include paths for windows.h")
        endif()
        set(_SYNTHRT_RC_FLAGS_INITIALIZED TRUE CACHE INTERNAL "")
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

# Make synthrt_add_unittest available to all test subdirectories. Loaded
# early (before any add_subdirectory that contains a unittests/ folder) so
# domain test directories (e.g. domains/ds-session/unittests) can use it
# even though the top-level unittests/ dir is processed last. The function
# only references Catch2 at call time, so defining it here is safe.
if(SYNTHRT_BUILD_TESTS)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/synthrt_unittest.cmake)
endif()

# ----------------------------------
# Add source modules
# ----------------------------------
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
# Load synthrt_declare_target macro (unified BuildAPI entry point) so all
# lib/, domains/ and plugins/ subdirectories can call it. Must be included
# before any add_subdirectory that uses synthrt_declare_target.
include(synthrtHelpers)
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
# VoicebankSession composes ds-bank, G2P and ds-infer, so it must be
# configured only after all three exported targets exist.
add_subdirectory(domains/ds-session)

# C ABI (composes VoicebankScanner + LanguageService + Runtime)
set(CMAKE_FOLDER "Libraries")
add_subdirectory(lib/C)

# Plugins
set(CMAKE_FOLDER "Plugins")
add_subdirectory(plugins)

# Tools
if(SYNTHRT_BUILD_TOOLS)
    set(CMAKE_FOLDER "Tools")
    add_subdirectory(tools)
endif()

# Aggregate targets: link core runtime + DiffSinger domain libraries.
# Per-target creation / alias / EXPORT_NAME / install(TARGETS EXPORT) is
# encapsulated in cmake/synthrtAggregate.cmake (CM-06); config-level install
# (export file, package config, resources) remains below.
include(synthrtAggregate)

synthrt_setup_aggregate(
    TARGET synthrt-synthrt
    ALIAS srt::synthrt
    EXPORT_NAME synthrt
    FOLDER "Domains"
    COMPONENTS srt::core srt::driver srt::s2p srt::svs srt-ds::bank srt-ds::session
)

synthrt_setup_aggregate(
    TARGET srt-diffsinger
    ALIAS srt::diffsinger
    EXPORT_NAME diffsinger
    FOLDER "Domains"
    COMPONENTS srt::g2p srt-ds::bank srt-ds::infer srt-ds::session
)

# ----------------------------------
# Export aggregate targets for find_package(synthrt)
# ----------------------------------
if(SYNTHRT_INSTALL)
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

    # The English package (Phonetic-Suite-Eng) is excluded by default until its
    # dictionary and model redistribution provenance is documented.
    #
    # BI-25: The exclusion is applied defensively in two layers to guarantee
    # that Phonetic-Suite-Eng is never installed:
    #   1. install(CODE remove_recurse) below runs at install time and deletes
    #      the directory if it was copied by any prior install(DIRECTORY) rule.
    #      This is the "cleanup" backstop.
    #   2. install(DIRECTORY ... PATTERN ... EXCLUDE) skips the directory during
    #      the copy itself. This is the primary exclusion mechanism.
    # The double exclusion is intentionally redundant: if a CMake version or
    # edge-case causes the PATTERN EXCLUDE to miss the directory, the
    # remove_recurse layer still ensures the install tree is clean. The
    # outcome is asserted by tests/packaging/cmake/AssertInstallTree.cmake
    # (Phonetic-Suite-Eng must not exist in the install tree).
    install(CODE
        "file(REMOVE_RECURSE \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/synthrt/G2pPackages/Phonetic-Suite-Eng\")"
    )
    install(DIRECTORY resources/G2pPackages
        DESTINATION ${CMAKE_INSTALL_DATADIR}/synthrt
        PATTERN "Phonetic-Suite-Eng" EXCLUDE
    )
endif()

if(SYNTHRT_BUILD_TESTS)
    set(CMAKE_FOLDER "Tests")
    add_subdirectory(unittests)
    add_subdirectory(tests)
endif()
