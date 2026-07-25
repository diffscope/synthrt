// PluginCommon.h 工具函数极端场景测试。
//
// 覆盖范围：
//   - validateFrameWidth: NaN / 0 / 负 / 无穷大 / 正常值（BF-43）
//   - checkDriverReady: driver=nullptr（BF-44）
//   - validateInitArgs: args=nullptr / name 不匹配 / name 匹配（BF-45）
//   - validateStartInput: input=nullptr / name 不匹配 / name 匹配（BF-46）
//   - getTypedConfig: spec=nullptr（BF-47）
//   - getTypedSchema: spec=nullptr（BF-48）
//   - openOnnxSession: driver=nullptr / session open 失败（BF-49）
//
// 这些用例反映 ds-editor-lite 调用 Inference::start() 时可能触发的极端情况：
// - SingerModelSession 在 stale 状态下被并发调用 → driver 已被 reset
// - 上游传入空 args / input 指针（防御性，正常 lite 不会发生）
// - 配置错误（frameWidth 为 NaN / 0）
// - ONNX 模型路径错误或损坏（session open 失败）
//
// PluginCommon.h 7 个函数均遵循 ARCH-01（不调用 setState）和 CODING-04
// （不含 mutex 加锁），故测试不需要 ITask 状态机与线程同步。

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Task/ITask.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>
#include <synthrt/SVS/InferenceContrib.h>

#include <inferutil/PluginCommon.h>

using namespace ds::infer::inferutil;
using srt::core::ErrorCode;
using srt::core::ErrorCategory;

namespace {

    // Minimal InferenceSession mock: open() 总是返回指定错误，用于测试
    // openOnnxSession 在 session 打开失败时的错误传播。
    // 其他 ITask 纯虚函数返回默认值（不被 PluginCommon.h 调用）。
    class FailOpenSession : public srt::driver::InferenceSession {
    public:
        srt::core::Expected<void> open(const std::filesystem::path &,
                                       const srt::core::NO<srt::driver::InferenceSessionOpenArgs> &) override {
            return srt::core::Error(srt::core::ErrorCode::InferenceStartFailed,
                                    "mock: session open failed");
        }
        srt::core::Expected<void> close() override { return srt::core::Expected<void>(); }
        bool isOpen() const override { return false; }
        int64_t id() const override { return 0; }

        srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
            start(const srt::core::NO<srt::core::TaskStartInput> &) override {
            return srt::core::Error(srt::core::Error::NotImplemented,
                                    "FailOpenSession::start not implemented");
        }
        bool stop() override { return false; }
        srt::core::NO<srt::core::TaskResult> result() const override { return nullptr; }
    };

    // Minimal InferenceDriver mock: createSession() 返回 FailOpenSession。
    class FailOpenDriver : public srt::driver::InferenceDriver {
    public:
        FailOpenDriver() {
            setObjectName("fail_open_driver");
        }
        std::string arch() const override { return "mock"; }
        std::string backend() const override { return "mock"; }
        srt::core::Expected<void> initialize(
            const srt::core::NO<srt::driver::InferenceDriverInitArgs> &) override {
            return srt::core::Expected<void>();
        }
        srt::core::NO<srt::driver::InferenceSession> createSession() override {
            return srt::core::NO<srt::driver::InferenceSession>(new FailOpenSession());
        }
    };

    constexpr auto kLogPrefix = "[Test]";

} // namespace

// ---------------------------------------------------------------------------
// BF-43: validateFrameWidth 极端值
// ---------------------------------------------------------------------------

TEST_CASE("BF-43 validateFrameWidth NaN returns InvalidArgument",
          "[plugincommon][extreme][bf-43]") {
    auto exp = validateFrameWidth(std::numeric_limits<double>::quiet_NaN(), kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find("frame width") != std::string::npos);
    REQUIRE(exp.error().message().find(kLogPrefix) != std::string::npos);
}

TEST_CASE("BF-43 validateFrameWidth positive infinity returns InvalidArgument",
          "[plugincommon][extreme][bf-43]") {
    auto exp = validateFrameWidth(std::numeric_limits<double>::infinity(), kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("BF-43 validateFrameWidth negative infinity returns InvalidArgument",
          "[plugincommon][extreme][bf-43]") {
    auto exp = validateFrameWidth(-std::numeric_limits<double>::infinity(), kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("BF-43 validateFrameWidth zero returns InvalidArgument",
          "[plugincommon][extreme][bf-43]") {
    auto exp = validateFrameWidth(0.0, kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("BF-43 validateFrameWidth negative returns InvalidArgument",
          "[plugincommon][extreme][bf-43]") {
    auto exp = validateFrameWidth(-0.005, kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("BF-43 validateFrameWidth very small positive succeeds",
          "[plugincommon][extreme][bf-43]") {
    // subnormal 正数也应当通过（仅校验 >0 && isfinite）
    auto exp = validateFrameWidth(1e-300, kLogPrefix);
    REQUIRE(exp.hasValue());
}

TEST_CASE("BF-43 validateFrameWidth normal positive succeeds",
          "[plugincommon][extreme][bf-43]") {
    auto exp = validateFrameWidth(0.005, kLogPrefix);
    REQUIRE(exp.hasValue());
}

TEST_CASE("BF-43 validateFrameWidth log prefix appears in error message",
          "[plugincommon][extreme][bf-43]") {
    // 错误消息必须保留 logPrefix（ROBUST-05 不丢失上下文）
    constexpr auto prefix = "[Duration]";
    auto exp = validateFrameWidth(0.0, prefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.error().message().find(prefix) != std::string::npos);
}

// ---------------------------------------------------------------------------
// BF-44: checkDriverReady
// ---------------------------------------------------------------------------

TEST_CASE("BF-44 checkDriverReady null driver returns InferenceStartFailed",
          "[plugincommon][extreme][bf-44]") {
    srt::core::NO<srt::driver::InferenceDriver> nullDriver;
    auto exp = checkDriverReady(nullDriver, kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InferenceStartFailed));
    REQUIRE(exp.error().message().find("driver") != std::string::npos);
    REQUIRE(exp.error().message().find(kLogPrefix) != std::string::npos);
}

TEST_CASE("BF-44 checkDriverReady non-null driver succeeds",
          "[plugincommon][extreme][bf-44]") {
    auto driver = srt::core::NO<srt::driver::InferenceDriver>(new FailOpenDriver());
    auto exp = checkDriverReady(driver, kLogPrefix);
    REQUIRE(exp.hasValue());
}

TEST_CASE("BF-44 checkDriverReady default-constructed NO is null",
          "[plugincommon][extreme][bf-44]") {
    // 默认构造的 NO<InferenceDriver> 等价于 nullptr（ROBUST-03 防空）
    srt::core::NO<srt::driver::InferenceDriver> defaultDriver;
    REQUIRE(!defaultDriver);
    auto exp = checkDriverReady(defaultDriver, kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InferenceStartFailed));
}

// ---------------------------------------------------------------------------
// BF-45: validateInitArgs
// ---------------------------------------------------------------------------

TEST_CASE("BF-45 validateInitArgs null args returns InvalidArgument",
          "[plugincommon][extreme][bf-45]") {
    srt::core::NO<srt::core::TaskInitArgs> nullArgs;
    auto exp = validateInitArgs(nullArgs, "acoustic", kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find(kLogPrefix) != std::string::npos);
}

TEST_CASE("BF-45 validateInitArgs name mismatch returns InvalidArgument",
          "[plugincommon][extreme][bf-45]") {
    auto args = srt::core::NO<srt::core::TaskInitArgs>::create("acoustic");
    auto exp = validateInitArgs(args, "duration", kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    // 错误消息应包含 expected 与 got 的对比
    REQUIRE(exp.error().message().find("expected") != std::string::npos);
    REQUIRE(exp.error().message().find("got") != std::string::npos);
    REQUIRE(exp.error().message().find("acoustic") != std::string::npos);
    REQUIRE(exp.error().message().find("duration") != std::string::npos);
}

TEST_CASE("BF-45 validateInitArgs name match succeeds",
          "[plugincommon][extreme][bf-45]") {
    auto args = srt::core::NO<srt::core::TaskInitArgs>::create("acoustic");
    auto exp = validateInitArgs(args, "acoustic", kLogPrefix);
    REQUIRE(exp.hasValue());
}

TEST_CASE("BF-45 validateInitArgs empty apiName mismatch returns error",
          "[plugincommon][extreme][bf-45]") {
    // args 名称为非空但 apiName 为空字符串：应不匹配并返回错误
    auto args = srt::core::NO<srt::core::TaskInitArgs>::create("acoustic");
    auto exp = validateInitArgs(args, std::string_view{}, kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
}

// ---------------------------------------------------------------------------
// BF-46: validateStartInput
// ---------------------------------------------------------------------------

TEST_CASE("BF-46 validateStartInput null input returns InvalidArgument",
          "[plugincommon][extreme][bf-46]") {
    srt::core::NO<srt::core::TaskStartInput> nullInput;
    auto exp = validateStartInput(nullInput, "acoustic", kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find(kLogPrefix) != std::string::npos);
    REQUIRE(exp.error().message().find("input") != std::string::npos);
}

TEST_CASE("BF-46 validateStartInput name mismatch returns InvalidArgument",
          "[plugincommon][extreme][bf-46]") {
    auto input = srt::core::NO<srt::core::TaskStartInput>::create("acoustic");
    auto exp = validateStartInput(input, "vocoder", kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find("expected") != std::string::npos);
    REQUIRE(exp.error().message().find("got") != std::string::npos);
    REQUIRE(exp.error().message().find("acoustic") != std::string::npos);
    REQUIRE(exp.error().message().find("vocoder") != std::string::npos);
}

TEST_CASE("BF-46 validateStartInput name match succeeds",
          "[plugincommon][extreme][bf-46]") {
    auto input = srt::core::NO<srt::core::TaskStartInput>::create("duration");
    auto exp = validateStartInput(input, "duration", kLogPrefix);
    REQUIRE(exp.hasValue());
}

TEST_CASE("BF-46 validateStartInput error message contains start keyword",
          "[plugincommon][extreme][bf-46]") {
    // 错误消息应包含 "start:" 以与 validateInitArgs 区分
    srt::core::NO<srt::core::TaskStartInput> nullInput;
    auto exp = validateStartInput(nullInput, "acoustic", kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.error().message().find("start") != std::string::npos);
}

// ---------------------------------------------------------------------------
// BF-47: getTypedConfig
// ---------------------------------------------------------------------------

TEST_CASE("BF-47 getTypedConfig null spec returns InvalidArgument",
          "[plugincommon][extreme][bf-47]") {
    // spec=nullptr 是最重要的极端情况：lite 调用 initialize() 时若 spec 未加载
    // 完成，会传入空 spec。ROBUST-03 要求防空。
    const srt::svs::InferenceSpec *nullSpec = nullptr;
    auto exp = getTypedConfig<srt::svs::InferenceConfiguration>(
        nullSpec, "ai.svs.AcousticInference", "acoustic", kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find("spec") != std::string::npos);
    REQUIRE(exp.error().message().find(kLogPrefix) != std::string::npos);
}

// 注：getTypedConfig 的 "configuration=nullptr" 与 "class/name 不匹配" 路径
// 需要 InferenceSpec 实例。InferenceSpec 构造函数是 protected，且
// configuration() 不是 virtual，无法通过派生类注入 mock。该路径由 5 个
// 插件 .cpp 的集成测试覆盖（加载真实 ONNX 模型时触发）。

// ---------------------------------------------------------------------------
// BF-48: getTypedSchema
// ---------------------------------------------------------------------------

TEST_CASE("BF-48 getTypedSchema null spec returns InvalidArgument",
          "[plugincommon][extreme][bf-48]") {
    const srt::svs::InferenceSpec *nullSpec = nullptr;
    auto exp = getTypedSchema<srt::svs::InferenceSchema>(
        nullSpec, "ai.svs.VarianceInference", "variance", kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find("spec") != std::string::npos);
    REQUIRE(exp.error().message().find(kLogPrefix) != std::string::npos);
}

// 注：getTypedSchema 的 "schema=nullptr" 与 "class/name 不匹配" 路径同 BF-47
// 注释，需集成测试覆盖。

// ---------------------------------------------------------------------------
// BF-49: openOnnxSession
// ---------------------------------------------------------------------------

TEST_CASE("BF-49 openOnnxSession null driver returns InvalidArgument",
          "[plugincommon][extreme][bf-49]") {
    srt::core::NO<srt::driver::InferenceDriver> nullDriver;
    auto exp = openOnnxSession(nullDriver, std::filesystem::path("dummy.onnx"),
                               false, "encoder", kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InvalidArgument));
    REQUIRE(exp.error().message().find("driver") != std::string::npos);
    REQUIRE(exp.error().message().find(kLogPrefix) != std::string::npos);
}

TEST_CASE("BF-49 openOnnxSession session open failure returns InferenceStartFailed",
          "[plugincommon][extreme][bf-49]") {
    // 模拟 ONNX 模型路径错误或文件损坏：FailOpenDriver 创建的 session 总是
    // open 失败。openOnnxSession 应将底层错误包装为带 logPrefix/sessionName
    // /modelPath 的 InferenceStartFailed 错误。
    auto driver = srt::core::NO<srt::driver::InferenceDriver>(new FailOpenDriver());
    const std::filesystem::path modelPath = "nonexistent/model.onnx";

    auto exp = openOnnxSession(driver, modelPath, false, "encoder", kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InferenceStartFailed));
    // 错误消息应包含 logPrefix、sessionName、modelPath（ROBUST-05 不丢失上下文）
    REQUIRE(exp.error().message().find(kLogPrefix) != std::string::npos);
    REQUIRE(exp.error().message().find("encoder") != std::string::npos);
    REQUIRE(exp.error().message().find("model") != std::string::npos);
}

TEST_CASE("BF-49 openOnnxSession error message contains session name for predictor",
          "[plugincommon][extreme][bf-49]") {
    // 双 session 插件（Duration/Pitch/Variance）的 predictor session 失败时
    // 错误消息应包含 "predictor" 以区分。
    auto driver = srt::core::NO<srt::driver::InferenceDriver>(new FailOpenDriver());
    auto exp = openOnnxSession(driver, std::filesystem::path("p.onnx"),
                               false, "predictor", "[Duration]");
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.error().message().find("predictor") != std::string::npos);
    REQUIRE(exp.error().message().find("[Duration]") != std::string::npos);
}

TEST_CASE("BF-49 openOnnxSession useCpu flag does not affect error path",
          "[plugincommon][extreme][bf-49]") {
    // useCpu=true 不影响错误路径（mock 不检查此标志）
    auto driver = srt::core::NO<srt::driver::InferenceDriver>(new FailOpenDriver());
    auto exp = openOnnxSession(driver, std::filesystem::path("cpu.onnx"),
                               true, "session", kLogPrefix);
    REQUIRE(!exp.hasValue());
    REQUIRE(exp.isError(ErrorCode::InferenceStartFailed));
}

// ---------------------------------------------------------------------------
// 跨函数一致性：logPrefix 保留（ROBUST-05）
// ---------------------------------------------------------------------------

TEST_CASE("ROBUST-05 all functions preserve logPrefix in error messages",
          "[plugincommon][extreme][robust-05]") {
    constexpr auto prefix = "[CustomPrefix]";

    // validateFrameWidth
    REQUIRE(validateFrameWidth(0.0, prefix).error().message().find(prefix) !=
            std::string::npos);

    // checkDriverReady
    srt::core::NO<srt::driver::InferenceDriver> nullDriver;
    REQUIRE(checkDriverReady(nullDriver, prefix).error().message().find(prefix) !=
            std::string::npos);

    // validateInitArgs
    srt::core::NO<srt::core::TaskInitArgs> nullArgs;
    REQUIRE(validateInitArgs(nullArgs, "x", prefix).error().message().find(prefix) !=
            std::string::npos);

    // validateStartInput
    srt::core::NO<srt::core::TaskStartInput> nullInput;
    REQUIRE(validateStartInput(nullInput, "x", prefix).error().message().find(prefix) !=
            std::string::npos);

    // getTypedConfig
    REQUIRE(getTypedConfig<srt::svs::InferenceConfiguration>(
                nullptr, "c", "n", prefix).error().message().find(prefix) !=
            std::string::npos);

    // getTypedSchema
    REQUIRE(getTypedSchema<srt::svs::InferenceSchema>(
                nullptr, "c", "n", prefix).error().message().find(prefix) !=
            std::string::npos);

    // openOnnxSession
    REQUIRE(openOnnxSession(nullDriver, std::filesystem::path("x.onnx"),
                            false, "s", prefix).error().message().find(prefix) !=
            std::string::npos);
}

// ---------------------------------------------------------------------------
// 错误码一致性：所有"空指针"路径返回 InvalidArgument（除 checkDriverReady）
// ---------------------------------------------------------------------------

TEST_CASE("Error code consistency: null pointer paths return InvalidArgument",
          "[plugincommon][extreme][error-codes]") {
    // 除 checkDriverReady（InferenceStartFailed）外，所有空指针路径都应返回
    // InvalidArgument。这反映了 lite 调用模式的差异：
    // - validateXxx/getTypedXxx/openOnnxSession(null driver) 是参数错误
    // - checkDriverReady 是运行时状态错误（driver 应已初始化但未初始化）

    srt::core::NO<srt::core::TaskInitArgs> nullArgs;
    REQUIRE(validateInitArgs(nullArgs, "x", kLogPrefix).isError(ErrorCode::InvalidArgument));

    srt::core::NO<srt::core::TaskStartInput> nullInput;
    REQUIRE(validateStartInput(nullInput, "x", kLogPrefix).isError(ErrorCode::InvalidArgument));

    REQUIRE(getTypedConfig<srt::svs::InferenceConfiguration>(
                nullptr, "c", "n", kLogPrefix).isError(ErrorCode::InvalidArgument));

    REQUIRE(getTypedSchema<srt::svs::InferenceSchema>(
                nullptr, "c", "n", kLogPrefix).isError(ErrorCode::InvalidArgument));

    srt::core::NO<srt::driver::InferenceDriver> nullDriver;
    REQUIRE(openOnnxSession(nullDriver, std::filesystem::path("x.onnx"),
                            false, "s", kLogPrefix).isError(ErrorCode::InvalidArgument));

    // checkDriverReady 是例外：driver 未初始化属于运行时状态错误
    REQUIRE(checkDriverReady(nullDriver, kLogPrefix).isError(ErrorCode::InferenceStartFailed));
}
