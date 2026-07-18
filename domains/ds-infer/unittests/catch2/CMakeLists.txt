# Catch2 v3 unit tests for ds-infer inferutil helpers and srt::core version utils.
#
# These tests cover Algorithm.h (arange/interpolate/resample/fillRestMidi),
# TensorHelper.h, Speedup.h, VersionUtils, InputWord helpers, and
# SpeakerEmbedding — the shared helpers used by all 5 DiffSinger inference
# plugins. Bug regression tests for BF-04..20 are included where noted.
# Real-world scenario tests mirror actual ds-editor-lite call patterns.

if(NOT TARGET Catch2::Catch2WithMain)
    find_package(Catch2 3 CONFIG REQUIRED)
endif()

include(Catch)

add_executable(tst-ds-infer-catch2
    test_algorithm.cpp
    test_algorithm_realworld.cpp
    test_algorithm_extreme.cpp
    test_tensor_helper.cpp
    test_version_utils.cpp
    test_version_utils_complex.cpp
    test_input_word.cpp
    test_input_word_realworld.cpp
    test_input_word_extreme.cpp
    test_linguistic_encoder.cpp
    test_speaker_embedding.cpp
    test_speaker_embedding_extreme.cpp
    test_speaker_embedding_custom_mix.cpp
    test_modelset.cpp
    test_modelset_errors.cpp
    test_modelset_trace.cpp
    test_modelset_stale_retry.cpp
    test_inference_service.cpp
    test_error_collector.cpp
    test_speaker_mapper.cpp
    test_model_registry.cpp
    test_singer_resolver_ambiguity.cpp
    test_package_isolation.cpp
)

# SpeakerMapper.h is a private header of srt-ds-infer (lives in lib/), so
# expose that directory to the test target.
target_include_directories(tst-ds-infer-catch2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib
)

target_link_libraries(tst-ds-infer-catch2 PRIVATE
    Catch2::Catch2WithMain
    inferutil
    synthrt::core
    srt-ds::infer
)

set_target_properties(tst-ds-infer-catch2 PROPERTIES FOLDER "Tests")

catch_discover_tests(tst-ds-infer-catch2)
