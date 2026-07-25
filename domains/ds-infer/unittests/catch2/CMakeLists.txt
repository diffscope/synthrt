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
    tst_algorithm.cpp
    tst_algorithm_realworld.cpp
    tst_algorithm_extreme.cpp
    tst_tensor_helper.cpp
    tst_version_utils.cpp
    tst_version_utils_complex.cpp
    tst_input_word.cpp
    tst_input_word_realworld.cpp
    tst_input_word_extreme.cpp
    tst_linguistic_encoder.cpp
    tst_speaker_embedding.cpp
    tst_speaker_embedding_extreme.cpp
    tst_speaker_embedding_custom_mix.cpp
    tst_modelset.cpp
    tst_modelset_errors.cpp
    tst_modelset_trace.cpp
    tst_modelset_stale_retry.cpp
    tst_inference_service.cpp
    tst_error_collector.cpp
    tst_speaker_mapper.cpp
    tst_model_registry.cpp
    tst_singer_resolver_ambiguity.cpp
    tst_package_isolation.cpp
    tst_plugin_common_extreme.cpp
    # T-12 DiffSinger 插件 start() 代码路径集成测试 (P1, L2 分级)：
    # Mock InferenceSession + 复现插件 start() 早期错误分支与异常输出校验。
    # 覆盖 BF-35/36/37/60/61 回归（DF-001~DF-007）。
    tst_diffsinger_plugin_integration.cpp
    # T-06 推理流水线超大输入压力测试 (P2, BF-46 回归)：
    # 10000 phoneme 输入、空 notes、超长单音符（3600s）、100+ speaker mix。
    # L1 路径直接调用 inferutil 函数，无需 ONNX Runtime / 插件 DLL。
    tst_inference_stress.cpp
    # Edge-case matrix (test-matrix-expansion.md §1.9): INF-001 ~ INF-012.
    # Lives in the parent unittests/ dir alongside tst_infer_edge.cpp.
    ../tst_infer_edge.cpp
)

# SpeakerMapper.h is a private header of srt-ds-infer (lives in lib/), so
# expose that directory to the test target.
target_include_directories(tst-ds-infer-catch2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../lib
)

target_link_libraries(tst-ds-infer-catch2 PRIVATE
    Catch2::Catch2WithMain
    inferutil
    srt::core
    srt-ds::infer
)

set_target_properties(tst-ds-infer-catch2 PROPERTIES FOLDER "Tests")

catch_discover_tests(tst-ds-infer-catch2)
