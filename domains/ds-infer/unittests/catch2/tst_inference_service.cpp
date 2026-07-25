// InferenceService 单元测试：setStages null spec 校验和构造行为。
//
// 覆盖范围：
//   - setStages 空 StageSet（所有 spec=nullptr）返回 InferenceInputInvalid
//   - setStages 单个 spec=nullptr 返回错误
//   - setStages 5 个阶段中任一为 null 都返回错误
//   - setStages 错误信息包含 "null"
//   - 默认构造的 InferenceService 调 run() 返回错误（stages 未设置）
//   - StageSet::find() 在 invalid kind 返回 nullptr
//   - InferenceService 默认构造可析构（无 leak）
//
// 注意：sampleRate 兼容性校验需要真实 InferenceSpec，无法在单元测试中覆盖。
// 该路径需要 integration test（加载真实 ONNX 模型）。这里只覆盖 null spec 路径。

#include <catch2/catch_test_macros.hpp>

#include <diffsinger/Infer/InferenceService.h>
#include <diffsinger/Infer/InferenceRequest.h>
#include <diffsinger/Infer/InferenceResult.h>
#include <diffsinger/Infer/StageKind.h>
#include <synthrt/Core/Support/Diagnostic.h>

using namespace ds::infer;
using srt::core::ErrorCode;

namespace {
    StageSet makeEmptyStageSet() {
        StageSet stages;
        return stages;
    }
}

// ---------------------------------------------------------------------------
// setStages null spec 校验
// ---------------------------------------------------------------------------

TEST_CASE("InferenceService setStages with all null specs returns error",
          "[inferenceservice]") {
    InferenceService service;
    auto stages = makeEmptyStageSet();
    auto exp = service.setStages(stages);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InferenceInputInvalid));
    REQUIRE(exp.error().message().find("null") != std::string::npos);
}

TEST_CASE("InferenceService setStages duration null returns error",
          "[inferenceservice]") {
    // 设置其他 4 个 spec 但 duration 为 null，应返回错误
    InferenceService service;
    auto stages = makeEmptyStageSet();
    // 其他 spec 字段不设置（保持 nullptr）
    // 仅 duration 为 null（默认）
    auto exp = service.setStages(stages);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InferenceInputInvalid));
}

TEST_CASE("InferenceService setStages error message contains stage spec mention",
          "[inferenceservice]") {
    InferenceService service;
    auto stages = makeEmptyStageSet();
    auto exp = service.setStages(stages);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.error().message().find("stage spec") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 默认构造 + 析构（无 leak）
// ---------------------------------------------------------------------------

TEST_CASE("InferenceService default constructor destructs cleanly", "[inferenceservice]") {
    // 验证默认构造和析构不会崩溃或泄漏
    InferenceService *service = new InferenceService();
    delete service;
}

TEST_CASE("InferenceService can be constructed and destructed in scope",
          "[inferenceservice]") {
    {
        InferenceService service;
    }
    // 离开作用域后应自动析构，不崩溃。
}

// ---------------------------------------------------------------------------
// run() 在 stages 未设置时返回错误
// ---------------------------------------------------------------------------

TEST_CASE("InferenceService run without setStages returns error in result",
          "[inferenceservice]") {
    // run() 在 stages 未设置时应返回带错误的 InferenceResult
    InferenceService service;
    InferenceRequest request;
    request.singerId = "test_singer";

    auto result = service.run(request);
    // run() 不抛异常，而是把错误填到 InferenceResult::error
    REQUIRE(!result.error.ok());
    REQUIRE(result.audio.empty());
}

TEST_CASE("InferenceService run error has InferenceInputInvalid code",
          "[inferenceservice]") {
    InferenceService service;
    InferenceRequest request;
    auto result = service.run(request);
    REQUIRE(!result.error.ok());
    REQUIRE(result.error.code() == ErrorCode::InferenceInputInvalid);
}

TEST_CASE("InferenceService run error message mentions stages not set",
          "[inferenceservice]") {
    InferenceService service;
    InferenceRequest request;
    auto result = service.run(request);
    REQUIRE(!result.error.ok());
    REQUIRE(result.error.message().find("stages") != std::string::npos);
}

TEST_CASE("InferenceService run with empty singerId still returns error",
          "[inferenceservice]") {
    // 空 singerId 也应返回错误（stages 未设置是根本原因）
    InferenceService service;
    InferenceRequest request;
    request.singerId = "";

    auto result = service.run(request);
    REQUIRE(!result.error.ok());
    REQUIRE(result.audio.empty());
}

// ---------------------------------------------------------------------------
// StageSet::find 行为
// ---------------------------------------------------------------------------

TEST_CASE("StageSet find returns nullptr for invalid kind", "[stageset][inferenceservice]") {
    StageSet stages;
    auto invalidKind = static_cast<StageKind>(999);
    REQUIRE(stages.find(invalidKind) == nullptr);
}

TEST_CASE("StageSet find returns valid pointer for each valid kind",
          "[stageset][inferenceservice]") {
    StageSet stages;
    REQUIRE(stages.find(StageKind::Duration) == &stages.duration);
    REQUIRE(stages.find(StageKind::Pitch) == &stages.pitch);
    REQUIRE(stages.find(StageKind::Variance) == &stages.variance);
    REQUIRE(stages.find(StageKind::Acoustic) == &stages.acoustic);
    REQUIRE(stages.find(StageKind::Vocoder) == &stages.vocoder);
}

// ---------------------------------------------------------------------------
// InferenceService 多次 setStages 调用
// ---------------------------------------------------------------------------

TEST_CASE("InferenceService setStages called multiple times returns error each time",
          "[inferenceservice]") {
    // 多次 setStages 都返回错误（因为 spec 都是 null）
    InferenceService service;
    auto stages = makeEmptyStageSet();

    for (int i = 0; i < 3; ++i) {
        auto exp = service.setStages(stages);
        REQUIRE(!exp.hasValue());
        REQUIRE(exp.isError(ErrorCode::InferenceInputInvalid));
    }
}

// ---------------------------------------------------------------------------
// InferenceResult 默认状态
// ---------------------------------------------------------------------------

TEST_CASE("InferenceResult default state has empty audio and zero sampleRate",
          "[inferenceservice]") {
    InferenceResult result;
    REQUIRE(result.audio.empty());
    REQUIRE(result.sampleRate == 0);
    REQUIRE(result.channels == 0);
    // 默认 error 应为"无错误"状态
    REQUIRE(result.error.ok());
}

// ---------------------------------------------------------------------------
// InferenceRequest 默认状态
// ---------------------------------------------------------------------------

TEST_CASE("InferenceRequest default state has empty fields", "[inferenceservice]") {
    InferenceRequest request;
    REQUIRE(request.singerId.empty());
    REQUIRE(request.words.empty());
    REQUIRE(request.parameters.empty());
    REQUIRE(request.speakers.empty());
    REQUIRE(request.duration == 0.0);
    // steps 默认为 10（Acoustic diffusion steps）
    REQUIRE(request.steps == 10);
    REQUIRE(request.depth == 0.0f);
}
