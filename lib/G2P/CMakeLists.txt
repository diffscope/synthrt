project(srt-g2p
    LANGUAGES CXX
    VERSION 0.0.1.0
    DESCRIPTION "synthrt v3 G2P (grapheme-to-phoneme) management"
)

synthrt_declare_target(srt_g2p "${PROJECT_NAME}" ${PROJECT_VERSION} "Team OpenVPI" 2023 "../../include")

# srt-g2p library
#
# Migration source: language-manager (D:\projects\language-manager).
# All public classes live under namespace srt::g2p::

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp")
list(FILTER _src EXCLUDE REGEX "/support/(Inferutil|common)/")

find_package(stdcorelib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# v2 Phase 1: LanguageService (srt::g2p) moved from ds-session to lib/G2P.
# It depends on ds::bank types (PackageParser, PackageManifest, etc.), so
# srt-ds::bank must be linked. ds-bank only depends on srt::core, so no
# circular dependency is introduced.
# LanguageService::resolveS2pResource() returns srt::s2p::LanguageResource,
# so srt::s2p is linked publicly. srt-s2p only depends on srt::core, so
# no circular dependency is introduced.
# A1: G2pOnnxSetup.cpp reuses the Runtime's "inference/dsdriver"
# srt::driver::InferenceDriver and wraps it with a CPU-only SessionFactory
# adapter, so srt::driver is linked publicly. srt-driver only depends on
# srt::core, so no circular dependency is introduced.
synthrt_add_library(${PROJECT_NAME} SHARED
    SOURCES ${_src}
    LINKS stdcorelib::stdcorelib srt::core srt::driver srt-ds::bank srt::s2p
    LINKS_PRIVATE nlohmann_json::nlohmann_json
    INCLUDE_PRIVATE ../../include/synthrt/G2P/** **
)

# sparsepp header-only dependency (used by Support/PhonemeDict.cpp).
find_package(SparsePP REQUIRED)
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${SPARSEPP_INCLUDE_DIRS}>
)

# Convenience alias matching the v3 roadmap convention (srt::g2p).
if(NOT TARGET srt::g2p)
    add_library(srt::g2p ALIAS ${PROJECT_NAME})
endif()

set(CMAKE_FOLDER "Libraries")
add_subdirectory(support)

synthrt_install()
