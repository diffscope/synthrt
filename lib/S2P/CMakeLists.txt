# srt-s2p: S2P (syllable-to-phoneme) runtime library.
#
# v9: migrated back from domains/ds-infer/lib/Phoneme/ to the general runtime
# layer. All S2P classes (DictionaryS2P/DirectS2P/MappingS2P/LuaS2P/
# LuaOnsetMarker/RuleOnsetMarker/LanguageResource/LuaScript) live under
# namespace srt::s2p and are exported from the independent srt-s2p shared
# library via SRT_S2P_EXPORT.
project(srt-s2p
    LANGUAGES CXX
    VERSION 0.0.1.0
    DESCRIPTION "synthrt S2P (syllable-to-phoneme) runtime"
)

set(srt-s2p_VAR_PREFIX srt_s2p)
set(SRT_S2P_INSTALL ${SYNTHRT_INSTALL})
set(SRT_S2P_VERSION ${PROJECT_VERSION})
set(SRT_S2P_INSTALL_NAME ${PROJECT_NAME})
set(SRT_S2P_AUTHOR "Team OpenVPI")
set(SRT_S2P_START_YEAR 2023)
set(SRT_S2P_INCLUDE_DIR ../../include)
include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")

file(GLOB _src "*.cpp" "internal/*.cpp")

# LuaJIT detection (migrated from ds-infer). Lua-backed S2P and onset marker
# implementations are excluded from the build when LuaJIT is unavailable.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LUAJIT IMPORTED_TARGET QUIET luajit)
endif()
set(SRT_S2P_HAS_LUA OFF)
set(_srt_s2p_lua_libs "")
set(_srt_s2p_lua_includes "")
if(LUAJIT_FOUND)
    set(SRT_S2P_HAS_LUA ON)
    set(_srt_s2p_lua_libs PkgConfig::LUAJIT)
else()
    find_path(_srt_s2p_lua_header
        NAMES lua.hpp lua.h
        PATHS
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/luajit"
            "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include/luajit"
            "$ENV{LUAJIT_INCLUDE_DIR}"
        PATH_SUFFIXES luajit
    )
    find_library(_srt_s2p_lua_lib
        NAMES lua51 luajit
        PATHS
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
            "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/lib"
            "$ENV{LUAJIT_LIBRARY_DIR}"
    )
    if(_srt_s2p_lua_header AND _srt_s2p_lua_lib)
        set(SRT_S2P_HAS_LUA ON)
        set(_srt_s2p_lua_includes ${_srt_s2p_lua_header})
        set(_srt_s2p_lua_libs ${_srt_s2p_lua_lib})
    endif()
endif()

if(NOT SRT_S2P_HAS_LUA)
    list(FILTER _src EXCLUDE REGEX "/Lua[A-Za-z]*\\.cpp$")
    list(FILTER _src EXCLUDE REGEX "/internal/LuaExecutionEnvironment\\.cpp$")
endif()

find_package(stdcorelib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

srt_s2p_add_library(${PROJECT_NAME} SHARED PREFIX srt_s2p
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib srt::core
    LINKS_PRIVATE nlohmann_json::nlohmann_json ${_srt_s2p_lua_libs}
    INCLUDE_PRIVATE ../../include/synthrt/S2P/** ** ${_srt_s2p_lua_includes}
)
target_compile_definitions(srt-s2p PRIVATE SRT_S2P_LIBRARY)
if(SRT_S2P_HAS_LUA)
    target_compile_definitions(srt-s2p PRIVATE SRT_S2P_HAS_LUA)
endif()

# Convenience aliases matching the v3 roadmap convention (srt::s2p).
if(NOT TARGET srt::s2p)
    add_library(srt::s2p ALIAS srt-s2p)
endif()
if(NOT TARGET synthrt::s2p)
    add_library(synthrt::s2p ALIAS srt-s2p)
endif()

srt_s2p_install()
