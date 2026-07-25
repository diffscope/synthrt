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

synthrt_declare_target(srt_s2p "${PROJECT_NAME}" ${PROJECT_VERSION} "Team OpenVPI" 2023 "../../include")

file(GLOB _src "*.cpp" "internal/*.cpp")

# Prefer pkg-config, but vcpkg does not provide LuaJIT pkg-config metadata on
# every platform. Keep its fallback paths encapsulated in an imported target.
if(SYNTHRT_ENABLE_LUAJIT)
    set(_srt_s2p_luajit_cache_key
        "${VCPKG_INSTALLED_DIR}|${VCPKG_TARGET_TRIPLET}|$ENV{VCPKG_ROOT}|$ENV{LUAJIT_INCLUDE_DIR}|$ENV{LUAJIT_LIBRARY_DIR}")
    if(DEFINED _srt_s2p_luajit_cached_key AND
       NOT _srt_s2p_luajit_cached_key STREQUAL _srt_s2p_luajit_cache_key)
        get_cmake_property(_srt_s2p_cache_variables CACHE_VARIABLES)
        foreach(_var IN LISTS _srt_s2p_cache_variables)
            if(_var MATCHES "^(LUAJIT_|__pkg_config_checked_LUAJIT$|_srt_s2p_lua_)")
                unset(${_var} CACHE)
                unset(${_var})
            endif()
        endforeach()
    endif()
    set(_srt_s2p_luajit_cached_key "${_srt_s2p_luajit_cache_key}"
        CACHE INTERNAL "LuaJIT search inputs" FORCE)

    # vcpkg installs separate Release/Debug libraries on Windows, while its
    # pkg-config metadata describes only one configuration. Use the explicit
    # imported target below so multi-config generators never mix CRT variants.
    if(NOT WIN32)
        find_package(PkgConfig QUIET)
    endif()
    if(PkgConfig_FOUND AND NOT WIN32)
        pkg_check_modules(LUAJIT IMPORTED_TARGET QUIET luajit)
    endif()

    if(TARGET PkgConfig::LUAJIT)
        set(_srt_s2p_luajit_target PkgConfig::LUAJIT)
    else()
        find_path(_srt_s2p_lua_header
            NAMES luajit.h
            PATHS
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/luajit"
                "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include/luajit"
                "$ENV{LUAJIT_INCLUDE_DIR}"
            PATH_SUFFIXES luajit
            NO_DEFAULT_PATH
        )
        find_library(_srt_s2p_lua_lib_release
            NAMES lua51 luajit luajit-5.1
            PATHS
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
                "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/lib"
                "$ENV{LUAJIT_LIBRARY_DIR}"
            NO_DEFAULT_PATH
        )
        find_library(_srt_s2p_lua_lib_debug
            NAMES lua51 luajit luajit-5.1
            PATHS
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
                "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/debug/lib"
                "$ENV{LUAJIT_LIBRARY_DIR}/debug"
            NO_DEFAULT_PATH
        )
        if(WIN32)
            find_file(_srt_s2p_lua_dll_release
                NAMES lua51.dll luajit.dll luajit-5.1.dll
                PATHS
                    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
                    "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/bin"
                    "$ENV{LUAJIT_LIBRARY_DIR}/../bin"
                NO_DEFAULT_PATH
            )
            find_file(_srt_s2p_lua_dll_debug
                NAMES lua51.dll luajit.dll luajit-5.1.dll
                PATHS
                    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
                    "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/debug/bin"
                    "$ENV{LUAJIT_LIBRARY_DIR}/../debug/bin"
                NO_DEFAULT_PATH
            )
        endif()
        if(NOT _srt_s2p_lua_header OR NOT _srt_s2p_lua_lib_release)
            message(FATAL_ERROR
                "SYNTHRT_ENABLE_LUAJIT is ON, but LuaJIT was not found")
        endif()
        set(_srt_s2p_needs_luajit_debug FALSE)
        if(CMAKE_CONFIGURATION_TYPES)
            list(FIND CMAKE_CONFIGURATION_TYPES Debug _srt_s2p_debug_config_index)
            if(NOT _srt_s2p_debug_config_index EQUAL -1)
                set(_srt_s2p_needs_luajit_debug TRUE)
            endif()
        elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_srt_s2p_needs_luajit_debug TRUE)
        endif()
        if(_srt_s2p_needs_luajit_debug AND NOT _srt_s2p_lua_lib_debug)
            message(FATAL_ERROR
                "SYNTHRT_ENABLE_LUAJIT is ON, but the LuaJIT Debug library was not found")
        endif()
        if(WIN32)
            add_library(srt::luajit SHARED IMPORTED)
        else()
            add_library(srt::luajit UNKNOWN IMPORTED)
        endif()
        set_target_properties(srt::luajit PROPERTIES
            IMPORTED_CONFIGURATIONS RELEASE
            IMPORTED_LOCATION_RELEASE "${_srt_s2p_lua_lib_release}"
            INTERFACE_INCLUDE_DIRECTORIES "${_srt_s2p_lua_header}"
        )
        if(_srt_s2p_lua_lib_debug)
            set_property(TARGET srt::luajit APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
            set_property(TARGET srt::luajit PROPERTY
                IMPORTED_LOCATION_DEBUG "${_srt_s2p_lua_lib_debug}")
        endif()
        if(WIN32 AND _srt_s2p_lua_dll_release)
            set_target_properties(srt::luajit PROPERTIES
                IMPORTED_LOCATION_RELEASE "${_srt_s2p_lua_dll_release}"
                IMPORTED_IMPLIB_RELEASE "${_srt_s2p_lua_lib_release}"
            )
        endif()
        if(WIN32 AND _srt_s2p_lua_dll_debug AND _srt_s2p_lua_lib_debug)
            set_target_properties(srt::luajit PROPERTIES
                IMPORTED_LOCATION_DEBUG "${_srt_s2p_lua_dll_debug}"
                IMPORTED_IMPLIB_DEBUG "${_srt_s2p_lua_lib_debug}"
            )
        endif()
        set_target_properties(srt::luajit PROPERTIES
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
            MAP_IMPORTED_CONFIG_MINSIZEREL Release
        )
        set(_srt_s2p_luajit_target srt::luajit)
    endif()
endif()

if(NOT SYNTHRT_ENABLE_LUAJIT)
    list(FILTER _src EXCLUDE REGEX "LuaExecutionEnvironment\\.cpp$")
endif()

find_package(stdcorelib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

synthrt_add_library(${PROJECT_NAME} SHARED PREFIX srt_s2p
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib srt::core
    LINKS_PRIVATE nlohmann_json::nlohmann_json ${_srt_s2p_luajit_target}
    INCLUDE_PRIVATE ../../include/synthrt/S2P/** **
)
target_compile_definitions(srt-s2p PRIVATE SRT_S2P_LIBRARY)
if(SYNTHRT_ENABLE_LUAJIT)
    target_compile_definitions(srt-s2p PRIVATE SRT_S2P_HAS_LUA)
endif()

# Convenience aliases matching the v3 roadmap convention (srt::s2p).
if(NOT TARGET srt::s2p)
    add_library(srt::s2p ALIAS srt-s2p)
endif()

synthrt_install()
