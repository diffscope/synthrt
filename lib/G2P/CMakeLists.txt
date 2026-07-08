project(srt-g2p
    LANGUAGES CXX
    VERSION 0.0.1.0
    DESCRIPTION "synthrt v3 G2P (grapheme-to-phoneme) management"
)

set(srt-g2p_VAR_PREFIX srt_g2p)
set(SRT_G2P_INSTALL ${SYNTHRT_INSTALL})
set(SRT_G2P_VERSION ${PROJECT_VERSION})
set(SRT_G2P_INSTALL_NAME ${PROJECT_NAME})
set(SRT_G2P_AUTHOR "Team OpenVPI")
set(SRT_G2P_START_YEAR 2023)
set(SRT_G2P_INCLUDE_DIR ../../include)
include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")

# srt-g2p library
#
# Migration source: language-manager (D:\projects\language-manager).
# All public classes live under namespace srt::g2p::

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp")
list(FILTER _src EXCLUDE REGEX "/Support/(InferUtil|Common)/")

find_package(stdcorelib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# v2 Phase 1: LanguageService (ds::lang) moved from ds-session to lib/G2P.
# It depends on ds::bank types (PackageParser, PackageManifest, etc.), so
# srt-ds::bank must be linked. ds-bank only depends on srt::core, so no
# circular dependency is introduced.
srt_g2p_add_library(${PROJECT_NAME} SHARED
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib srt::core srt-ds::bank
    LINKS_PRIVATE nlohmann_json::nlohmann_json
    INCLUDE_PRIVATE ../../include/synthrt/G2P/** **
)

# sparsepp header-only dependency (used by Support/PhonemeDict.cpp).
find_path(SPARSEPP_INCLUDE_DIRS "sparsepp/spp.h")
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${SPARSEPP_INCLUDE_DIRS}>
)

# Convenience alias matching the v3 roadmap convention (srt::g2p).
if(NOT TARGET srt::g2p)
    add_library(srt::g2p ALIAS ${PROJECT_NAME})
endif()
if(NOT TARGET synthrt::g2p)
    add_library(synthrt::g2p ALIAS ${PROJECT_NAME})
endif()

set(CMAKE_FOLDER "Libraries")
add_subdirectory(Support)

srt_g2p_install()
