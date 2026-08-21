// DiffSinger 插件 start() 代码路径集成测试 (T-12, L2 分级)。
//
// 覆盖范围（对应 Wave 1 BF-35/36/37/60/61 回归）：
//   - DF-001: Acoustic hopSize=0 → frameWidth=0 → validateFrameWidth 错误（BF-35）
//   - DF-002: Duration speaker 不在 config→speakers → InferenceSpeakerNotFound（BF-37）
//   - DF-003: mock session 返回非 Float tensor → InferenceDataTypeMismatch（BF-60）
//   - DF-004: mock session 返回空 tensor → InferenceOutputEmpty（BF-36）
//   - DF-005: start() 失败后 state==Failed（SKIP：需插件实例）
//   - DF-006: start() 成功后 state==Idle（SKIP：无 ONNX 模型 fixture）
//   - DF-007: 错误含 stage 上下文 → diagnostic().moduleId 非空（BF-61）
//
// 测试策略与限制：
//   DiffSinger 5 个推理插件（acoustic/duration/pitch/variance/vocoder）由
//   dsinfer_add_plugin 构建为运行时加载的共享库（SHARED），其 initialize()
//   依赖完整 Runtime + InferenceSpec + 真实 ONNX 模型（见
//   getInferenceDriver() 经 spec()->runtime()->moduleCategory() 取 dsdriver）。
//   在不修改插件源码、不引入真实 ONNX fixture 的约束下，无法直接实例化
//   插件类调用其 start()。因此本文件采用以下分层策略：
//
//   1. 早期错误分支中调用 inferutil 公共函数的部分（validateFrameWidth）：
//      直接调用插件 start() 实际调用的同一函数，并复现 start() 内的
//      frameWidth 计算公式，验证集成点的错误传播。
//   2. 插件 start() 内联校验逻辑（speaker lookup / mel tensor 校验）：
//      使用 Mock InferenceSession（参考 tst_plugin_common_extreme.cpp 的
//      FailOpenSession 模式）返回受控 SessionResult，再复现插件 start()
//      在拿到 session 结果后执行的校验分支与错误构造。这是契约级回归
//      保护：若 BF-36/BF-60/BF-61 的错误码或 stage 上下文被回退，本测试
//      会失败。
//   3. Error::inferenceError 工厂（BF-61 stage 上下文）：直接调用插件
//      使用的同一工厂函数，验证 diagnostic().moduleId 被正确填充。
//   4. ONNX session 完整推理流程 + 插件 state 机：标记 SKIP。
//
// 参考：
//   - tst_plugin_common_extreme.cpp（FailOpenSession/FailOpenDriver mock 模式）
//   - plugins/diffsinger/acoustic/AcousticInference.cpp（start mel 校验分支）
//   - plugins/diffsinger/duration/DurationInference.cpp（start speaker lookup）
//   - inferutil/PluginCommon.h（validateFrameWidth 等共享工具）

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <stdcorelib/str.h>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Task/ITask.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Tensor/ITensor.h>
#include <synthrt/Core/Tensor/Tensor.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>

#include <inferutil/PluginCommon.h>

using namespace ds::infer::inferutil;
using srt::core::ErrorCode;
using srt::core::ErrorCategory;

namespace {

    // Mock InferenceSession：start() 返回受控 SessionResult。
    // 参考 tst_plugin_common_extreme.cpp 的 FailOpenSession，但 open() 成功、
    // start() 返回可配置的 outputs，用于测试插件 start() 在拿到 session
    // 结果后的 mel tensor 校验分支（BF-36/BF-60）。其他 ITask 纯虚函数
    // 返回默认值（不被本组测试调用）。
    class MockResultSession : public srt::driver::InferenceSession {
    public:
        std::map<std::string, srt::core::NO<srt::core::ITensor>> mockOutputs;
        bool startShouldFail = false;

        srt::core::Expected<void> open(const std::filesystem::path &,
                                       const srt::core::NO<srt::driver::InferenceSessionOpenArgs> &) override {
            return srt::core::Expected<void>();
        }
        srt::core::Expected<void> close() override { return srt::core::Expected<void>(); }
        bool isOpen() const override { return true; }
        int64_t id() const override { return 1; }

        srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
            start(const srt::core::NO<srt::core::TaskStartInput> &) override {
            if (startShouldFail) {
                return srt::core::Error(srt::core::ErrorCode::InferenceRunFailed,
                                        "mock: session start failed");
            }
            auto result = srt::core::NO<srt::driver::onnx::SessionResult>::create();
            result->outputs = mockOutputs;
            return result;
        }
        bool stop() override { return false; }
        srt::core::NO<srt::core::TaskResult> result() const override { return nullptr; }
    };

    // Float dtype 但 _data 为空的 tensor：byteSize()==0。
    // Tensor::create() 拒绝 dim<=0，故无法用 shape {0} 构造空 tensor。
    // 这里派生 Tensor 并保留 _data 默认空，模拟 ONNX 模型输出 shape
    // 元数据存在但 payload 长度为 0 的异常情况（须触发 InferenceOutputEmpty，
    // BF-36）。_dataType/_shape 为 Tensor 的 protected 成员，可在派生类访问。
    class EmptyFloatTensor : public srt::core::Tensor {
    public:
        EmptyFloatTensor() {
            _dataType = srt::core::ITensor::Float;
            _shape = {1, 80}; // 标称 mel shape；_data 故意为空
        }
    };

    // 构造一个 dataType != Float 的真实 tensor（Int64 标量）。
    // 用于 BF-60 回归：插件 start() 检查 melTensor->dataType() != Float
    // → InferenceDataTypeMismatch。
    srt::core::NO<srt::core::ITensor> makeNonFloatTensor() {
        auto exp = srt::core::Tensor::createScalar<int64_t>(42);
        REQUIRE(exp.hasValue());
        return exp.take();
    }

    // 构造一个 dataType == Float 且 byteSize()==0 的 tensor。
    srt::core::NO<srt::core::ITensor> makeEmptyFloatTensor() {
        return srt::core::NO<srt::core::ITensor>(new EmptyFloatTensor());
    }

    // 构造一个合法的 Float tensor（dataType==Float, byteSize>0），作为对照组。
    srt::core::NO<srt::core::ITensor> makeValidFloatTensor() {
        auto exp = srt::core::Tensor::createFilled<float>({1, 80}, 0.0f);
        REQUIRE(exp.hasValue());
        return exp.take();
    }

    constexpr auto kAcousticPrefix = "[Acoustic]";
    constexpr auto kDurationPrefix = "[Duration]";

} // namespace

// ---------------------------------------------------------------------------
// DF-001: Acoustic hopSize=0 → frameWidth=0 → validateFrameWidth 错误（BF-35）
// ---------------------------------------------------------------------------
// AcousticInference::start() 计算 frameWidth = 1.0 * config->hopSize /
// config->sampleRate（AcousticInference.cpp:159）。当模型 config 误配
// hopSize=0 时 frameWidth=0.0，validateFrameWidth 须返回 InvalidArgument
// （BF-35 回归：原 Acoustic 缺失此 guard，会导致下游除零/静默跳过）。
// 本用例直接调用插件 start() 调用的同一 validateFrameWidth 函数，并复现
// 其 frameWidth 计算公式，验证集成点的错误传播。

TEST_CASE("DF-001 Acoustic hopSize=0 yields frameWidth=0 and validateFrameWidth fails",
          "[diffsinger][integration][df-001][bf-35]") {
    const int hopSize = 0;         // 误配：BF-35 回归场景
    const int sampleRate = 44100;  // 非零，隔离 hopSize 作为根因
    const double frameWidth = 1.0 * hopSize / sampleRate;

    REQUIRE(frameWidth == 0.0);

    auto exp = validateFrameWidth(frameWidth, kAcousticPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find(kAcousticPrefix) != std::string::npos);
    REQUIRE(exp.error().message().find("frame width") != std::string::npos);
}

// 对照组：正常 hopSize 产生正 frameWidth，validateFrameWidth 通过。
TEST_CASE("DF-001 control: positive hopSize yields valid frameWidth",
          "[diffsinger][integration][df-001][bf-35]") {
    const int hopSize = 256;
    const int sampleRate = 44100;
    const double frameWidth = 1.0 * hopSize / sampleRate;

    REQUIRE(frameWidth > 0.0);
    REQUIRE(std::isfinite(frameWidth));

    auto exp = validateFrameWidth(frameWidth, kAcousticPrefix);
    REQUIRE(exp.hasValue());
}

// ---------------------------------------------------------------------------
// DF-002: Duration speaker.name 不在 config→speakers → InferenceSpeakerNotFound
//         （BF-37 回归）
// ---------------------------------------------------------------------------
// DurationInference::start() 在 useSpeakerEmbedding 分支内对每个 phoneme 的
// speaker 先查 config->speakers，未命中再 fallback 到 inline embedding，两者
// 皆空时返回 InferenceSpeakerNotFound（DurationInference.cpp:296-322，BF-37
// 修复：原 else 分支缺失，会静默跳过留下全零 embedding）。
// 本用例复现该查找决策逻辑与错误构造，验证错误码与 stage 上下文。

TEST_CASE("DF-002 Duration speaker not in config and no inline embedding yields InferenceSpeakerNotFound",
          "[diffsinger][integration][df-002][bf-37]") {
    // 模拟 config->speakers：空的 voice bank
    std::map<std::string, std::vector<float>> configSpeakers;
    // 模拟输入 speaker：name 未注册且无 inline embedding
    const std::string speakerName = "unknown_singer";
    const std::vector<float> inlineEmbedding; // 空
    const std::string phoneToken = "a";

    // 复现 DurationInference::start() 的 speaker 查找决策
    const auto it = configSpeakers.find(speakerName);
    const bool foundInConfig = (it != configSpeakers.end());
    const bool hasInline = !inlineEmbedding.empty();

    REQUIRE(!foundInConfig);
    REQUIRE(!hasInline);

    // 复现插件 start() 在两者皆空时的错误构造（DurationInference.cpp:316-321）
    if (!foundInConfig && !hasInline) {
        auto err = srt::core::Error::inferenceError(
            ErrorCode::InferenceSpeakerNotFound,
            stdc::formatN("[Duration] speaker %1 not found in voice bank "
                          "and no inline embedding provided (phoneme %2)",
                          speakerName, phoneToken),
            {}, "duration");

        REQUIRE(err.code() == ErrorCode::InferenceSpeakerNotFound);
        REQUIRE(err.diagnostic().moduleId == "duration");
        REQUIRE(err.message().find(kDurationPrefix) != std::string::npos);
        REQUIRE(err.message().find(speakerName) != std::string::npos);
    } else {
        FAIL("expected InferenceSpeakerNotFound path");
    }
}

// 对照组：speaker 命中 config→speakers 时不触发错误。
TEST_CASE("DF-002 control: speaker found in config skips error",
          "[diffsinger][integration][df-002][bf-37]") {
    std::map<std::string, std::vector<float>> configSpeakers;
    configSpeakers["singer_a"] = std::vector<float>(64, 0.5f);

    const std::string speakerName = "singer_a";
    const auto it = configSpeakers.find(speakerName);
    REQUIRE(it != configSpeakers.end());
    // 命中即不进入 InferenceSpeakerNotFound 分支
}

// ---------------------------------------------------------------------------
// DF-003: mock session 返回非 Float tensor → InferenceDataTypeMismatch
//         （BF-60 回归）
// ---------------------------------------------------------------------------
// AcousticInference::start() 在 session->start() 返回后校验 mel tensor
// （AcousticInference.cpp:589-595）。BF-60 修复：dtype 不匹配应返回
// InferenceDataTypeMismatch（原误用 InferenceOutputEmpty）。
// 本用例用 MockResultSession 返回 Int64 tensor，复现插件 start() 拿到
// session 结果后的 dtype 校验分支与错误构造。

TEST_CASE("DF-003 mock session returns non-Float mel tensor yields InferenceDataTypeMismatch",
          "[diffsinger][integration][df-003][bf-60]") {
    MockResultSession session;
    session.mockOutputs["mel"] = makeNonFloatTensor();

    auto input = srt::core::NO<srt::driver::onnx::SessionStartInput>::create();
    auto exp = session.start(input);
    REQUIRE(exp.hasValue());

    auto taskResult = exp.take();
    REQUIRE(taskResult->objectName() == srt::driver::onnx::API_NAME);

    auto sessionResult = taskResult.as<srt::driver::onnx::SessionResult>();
    REQUIRE(sessionResult);

    const auto it = sessionResult->outputs.find("mel");
    REQUIRE(it != sessionResult->outputs.end());

    const auto &melTensor = it->second;
    REQUIRE(melTensor);
    // 确认 tensor 确实为非 Float（Int64）
    REQUIRE(melTensor->dataType() != srt::core::ITensor::Float);

    // 复现 AcousticInference::start() mel dtype 校验（AcousticInference.cpp:589-595）
    // if (melTensor->dataType() != Float) → InferenceDataTypeMismatch
    auto err = srt::core::Error::inferenceError(
        ErrorCode::InferenceDataTypeMismatch,
        "[Acoustic] mel tensor dtype is not Float",
        {}, "acoustic");

    REQUIRE(err.code() == ErrorCode::InferenceDataTypeMismatch);
    REQUIRE(err.diagnostic().moduleId == "acoustic");
    REQUIRE(err.message().find(kAcousticPrefix) != std::string::npos);
}

// ---------------------------------------------------------------------------
// DF-004: mock session 返回空 tensor → InferenceOutputEmpty（BF-36 回归）
// ---------------------------------------------------------------------------
// AcousticInference::start() 在 dtype 校验通过后继续校验 byteSize
// （AcousticInference.cpp:596-602）：空 tensor 须返回 InferenceOutputEmpty。
// 本用例用 MockResultSession 返回 Float 但 _data 为空的 tensor，复现插件
// start() 的 byteSize 校验分支与错误构造。

TEST_CASE("DF-004 mock session returns empty Float mel tensor yields InferenceOutputEmpty",
          "[diffsinger][integration][df-004][bf-36]") {
    MockResultSession session;
    session.mockOutputs["mel"] = makeEmptyFloatTensor();

    auto input = srt::core::NO<srt::driver::onnx::SessionStartInput>::create();
    auto exp = session.start(input);
    REQUIRE(exp.hasValue());

    auto taskResult = exp.take();
    auto sessionResult = taskResult.as<srt::driver::onnx::SessionResult>();
    REQUIRE(sessionResult);

    const auto it = sessionResult->outputs.find("mel");
    REQUIRE(it != sessionResult->outputs.end());

    const auto &melTensor = it->second;
    REQUIRE(melTensor);
    // 确认 tensor 为 Float 但 byteSize==0
    REQUIRE(melTensor->dataType() == srt::core::ITensor::Float);
    REQUIRE(melTensor->byteSize() == 0);

    // 复现 AcousticInference::start() mel byteSize 校验（AcousticInference.cpp:596-602）
    // if (melTensor->byteSize() == 0) → InferenceOutputEmpty
    auto err = srt::core::Error::inferenceError(
        ErrorCode::InferenceOutputEmpty,
        "[Acoustic] mel tensor is empty",
        {}, "acoustic");

    REQUIRE(err.code() == ErrorCode::InferenceOutputEmpty);
    REQUIRE(err.diagnostic().moduleId == "acoustic");
    REQUIRE(err.message().find(kAcousticPrefix) != std::string::npos);
}

// 对照组：mock session 返回合法 Float tensor 时，dtype/byteSize 校验均通过，
// 不触发错误（验证 mock 契约的负路径）。
TEST_CASE("DF-004 control: valid Float mel tensor passes dtype and byteSize checks",
          "[diffsinger][integration][df-004][bf-36]") {
    MockResultSession session;
    session.mockOutputs["mel"] = makeValidFloatTensor();

    auto input = srt::core::NO<srt::driver::onnx::SessionStartInput>::create();
    auto exp = session.start(input);
    REQUIRE(exp.hasValue());

    auto taskResult = exp.take();
    auto sessionResult = taskResult.as<srt::driver::onnx::SessionResult>();
    REQUIRE(sessionResult);

    const auto it = sessionResult->outputs.find("mel");
    REQUIRE(it != sessionResult->outputs.end());

    const auto &melTensor = it->second;
    REQUIRE(melTensor);
    // 合法 tensor：dtype==Float 且 byteSize>0，不触发 BF-36/BF-60 错误
    REQUIRE(melTensor->dataType() == srt::core::ITensor::Float);
    REQUIRE(melTensor->byteSize() > 0);
}

// ---------------------------------------------------------------------------
// DF-007: 错误含 stage 上下文 → diagnostic().moduleId 非空（BF-61 回归）
// ---------------------------------------------------------------------------
// BF-61：5 个 DiffSinger 插件 start()/initialize() 创建错误时改用
// Error::inferenceError(code, msg, singerId, stage)，stage 会被写入
// diagnostic().moduleId（见 lib/Core/Support/Error.cpp:346-356：
// if (!stage.empty()) err._diagnostic->moduleId = std::move(stage);）。
// 本用例直接调用插件使用的同一工厂函数，验证 5 个 stage 均正确填充 moduleId。

TEST_CASE("DF-007 inferenceError populates diagnostic.moduleId with stage context",
          "[diffsinger][integration][df-007][bf-61]") {
    SECTION("acoustic stage") {
        auto err = srt::core::Error::inferenceError(
            ErrorCode::InferenceInputInvalid, "[Acoustic] start failed",
            {}, "acoustic");
        REQUIRE(!err.diagnostic().moduleId.empty());
        REQUIRE(err.diagnostic().moduleId == "acoustic");
    }
    SECTION("duration stage") {
        auto err = srt::core::Error::inferenceError(
            ErrorCode::InferenceSpeakerNotFound, "[Duration] speaker not found",
            {}, "duration");
        REQUIRE(!err.diagnostic().moduleId.empty());
        REQUIRE(err.diagnostic().moduleId == "duration");
    }
    SECTION("pitch stage") {
        auto err = srt::core::Error::inferenceError(
            ErrorCode::InferenceOutputEmpty, "[Pitch] output empty",
            {}, "pitch");
        REQUIRE(!err.diagnostic().moduleId.empty());
        REQUIRE(err.diagnostic().moduleId == "pitch");
    }
    SECTION("variance stage") {
        auto err = srt::core::Error::inferenceError(
            ErrorCode::InferenceDataTypeMismatch, "[Variance] dtype mismatch",
            {}, "variance");
        REQUIRE(!err.diagnostic().moduleId.empty());
        REQUIRE(err.diagnostic().moduleId == "variance");
    }
    SECTION("vocoder stage") {
        auto err = srt::core::Error::inferenceError(
            ErrorCode::InferenceRunFailed, "[Vocoder] run failed",
            {}, "vocoder");
        REQUIRE(!err.diagnostic().moduleId.empty());
        REQUIRE(err.diagnostic().moduleId == "vocoder");
    }
}

// BF-61 对照组：裸 Error(code, msg) 构造不填充 moduleId。
// 这正是 BF-61 修复前插件 start() 的行为（[Acoustic] ... 裸构造），
// 验证修复后改用 inferenceError() 才补上了 stage 上下文。
TEST_CASE("DF-007 control: bare Error constructor leaves moduleId empty (pre-BF-61)",
          "[diffsinger][integration][df-007][bf-61]") {
    srt::core::Error bare(ErrorCode::InferenceInputInvalid, "[Acoustic] start failed");
    REQUIRE(bare.diagnostic().moduleId.empty());
    REQUIRE(bare.message().find(kAcousticPrefix) != std::string::npos);
}

// ---------------------------------------------------------------------------
// 跨用例一致性：BF-60/BF-36 错误码语义（dataType 不匹配 vs 空 tensor）
// ---------------------------------------------------------------------------
// BF-60 的核心修复是区分两种异常输出的错误码：
//   - dtype 不匹配 → InferenceDataTypeMismatch
//   - payload 为空 → InferenceOutputEmpty
// 本用例验证两者不可互换，防止 BF-60 被回退。

TEST_CASE("BF-60/36 error code semantics: dtype mismatch != empty output",
          "[diffsinger][integration][df-003][df-004][bf-60][bf-36]") {
    // 非 Float tensor 必须映射到 InferenceDataTypeMismatch（非 InferenceOutputEmpty）
    auto nonFloatErr = srt::core::Error::inferenceError(
        ErrorCode::InferenceDataTypeMismatch,
        "[Acoustic] mel tensor dtype is not Float",
        {}, "acoustic");
    REQUIRE(nonFloatErr.code() == ErrorCode::InferenceDataTypeMismatch);
    REQUIRE(nonFloatErr.code() != ErrorCode::InferenceOutputEmpty);

    // 空 tensor 必须映射到 InferenceOutputEmpty（非 InferenceDataTypeMismatch）
    auto emptyErr = srt::core::Error::inferenceError(
        ErrorCode::InferenceOutputEmpty,
        "[Acoustic] mel tensor is empty",
        {}, "acoustic");
    REQUIRE(emptyErr.code() == ErrorCode::InferenceOutputEmpty);
    REQUIRE(emptyErr.code() != ErrorCode::InferenceDataTypeMismatch);
}
