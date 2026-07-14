# srt-ds-infer: DiffSinger inference domain library (v4 target).
#
# Sources are globbed from lib/. The glob picks up:
#   - dsinfer_api.cpp        : dsinfer_abi_version() C export (single symbol anchor)
#   - InferenceService.cpp   : v4 InferenceService stub (ctor/dtor + run() -> NotImplemented)
#   - ModelRegistry.cpp      : ModelRegistry (bind() caches by (packageId, inferenceId))
#   - SpeakerMapper.cpp      : SpeakerMapper (singer speaker -> model speaker, keyed by (packageId, inferenceId))
#
# S2P/LanguageResource implementations were migrated back to the general runtime
# layer (lib/S2P/, namespace srt::s2p) in v9; ds-infer now links srt::s2p.
# LuaJIT detection also moved to lib/S2P/CMakeLists.txt.
#
# The util sublibraries under Util/ are built separately via add_subdirectory(Util);
# they remain standalone static targets (inferutil, inputparser, wavfile) consumed
# by plugins and tools.
#
# The Util/ sublibs use the ds::infer::inferutil namespace (v9: renamed from
# legacy srt::svs::inferutil).
file(GLOB _src "*.cpp")

find_package(stdcorelib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

dsinfer_add_library(srt-ds-infer SHARED PREFIX dsinfer
    SOURCES ${_src}
    LINKS srt::core srt::driver srt-ds::bank srt::s2p synthrt::svs
    LINKS_PRIVATE nlohmann_json::nlohmann_json
    INCLUDE_PRIVATE ../../../include/diffsinger/Infer/** **
)
target_compile_definitions(srt-ds-infer PRIVATE DSINFER_LIBRARY)

find_path(SPARSEPP_INCLUDE_DIRS "sparsepp/spp.h")
# Expose include/diffsinger/Infer/ as PUBLIC so consumers (CLI, lite) can
# resolve <dsinfer/...> headers transitively included by public headers such as
# InferenceRequest.h -> CommonApiL1.h -> <dsinfer/Core/ParamTag.h>.
target_include_directories(srt-ds-infer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../../include/diffsinger/Infer>
    $<BUILD_INTERFACE:${SPARSEPP_INCLUDE_DIRS}>
)

if(NOT TARGET srt-ds::infer)
    add_library(srt-ds::infer ALIAS srt-ds-infer)
endif()

# Util sublibraries relocated from domains/ds-infer/util/ (v4 Phase 7).
# Added after srt-ds-infer so they can link srt-ds::infer.
add_subdirectory(Util)
