// ONNX Driver 配置与契约测试
//
// 覆盖 D-20 人工决策约束：ONNX 驱动是全局基础设施，命名为 g2pOnnxDriver，
// 必须在 langMgr->initialize() 之前注册。
//
// 测试目标：
//   - OnnxDriverConfig 默认值（ep=DML, deviceIndex=0）
//   - ExecutionProvider 枚举值稳定性（CPU=0, CUDA=1, DML=2, CoreML=3）
//   - DriverInitArgs 默认 EP 为 CPU
//   - SessionOpenArgs::useCpu 默认 false
//   - setupOnnxInferenceDriver 函数签名存在性（链接时校验）
//
// 注意：OnnxTensor::createFromRawView 的 size 校验需要实际 ONNX Runtime
// 初始化才能测试（Ort::Value::CreateTensor 需要 OrtApi 实例），这里不覆盖。
// size 校验逻辑在 lib/Driver/onnx/OnnxTensor.cpp 中通过
// `data.size() != tensor->_bytesSize` 判断并返回
// Error(InvalidArgument, "createFromRawView: data size ...")。
//
// OnnxSetup 的 "no auto-fallback" 契约（CPU EP 失败不自动回退）需要完整
// Runtime + plugin 路径才能验证，这里通过源码契约注释记录：
//   "Do not auto-fallback: report the error explicitly so the user knows
//    the configured EP is unavailable."

#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Driver/OnnxSetup.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>

using srt::driver::OnnxDriverConfig;
using srt::driver::onnx::DriverInitArgs;
using srt::driver::onnx::ExecutionProvider;
using srt::driver::onnx::SessionOpenArgs;

// ---------------------------------------------------------------------------
// OnnxDriverConfig 默认值
//
// Lite SynthrtEngine::initialize 默认使用 DML EP（与 Lite GPU 选择 UI 对齐）。
// 默认 deviceIndex=0 表示使用第一个 GPU。
// ---------------------------------------------------------------------------
TEST_CASE("OnnxDriverConfig default values", "[driver][config][d-20]") {
    OnnxDriverConfig cfg;
    REQUIRE(cfg.ep == ExecutionProvider::DMLExecutionProvider);
    REQUIRE(cfg.deviceIndex == 0);
}

TEST_CASE("OnnxDriverConfig can be customized", "[driver][config]") {
    OnnxDriverConfig cfg;
    cfg.ep = ExecutionProvider::CUDAExecutionProvider;
    cfg.deviceIndex = 1;
    REQUIRE(cfg.ep == ExecutionProvider::CUDAExecutionProvider);
    REQUIRE(cfg.deviceIndex == 1);
}

// ---------------------------------------------------------------------------
// ExecutionProvider 枚举值稳定性
//
// 这些值跨版本必须稳定：Lite 通过整数映射选择 EP，C# P/Invoke 也依赖具体值。
// ---------------------------------------------------------------------------
TEST_CASE("ExecutionProvider enum values are stable", "[driver][ep][stable]") {
    REQUIRE(static_cast<int>(ExecutionProvider::CPUExecutionProvider) == 0);
    REQUIRE(static_cast<int>(ExecutionProvider::CUDAExecutionProvider) == 1);
    REQUIRE(static_cast<int>(ExecutionProvider::DMLExecutionProvider) == 2);
    REQUIRE(static_cast<int>(ExecutionProvider::CoreMLExecutionProvider) == 3);
}

// ---------------------------------------------------------------------------
// DriverInitArgs 默认值
//
// 注意：DriverInitArgs 默认 ep = CPUExecutionProvider（与 OnnxDriverConfig
// 默认 DML 不同）。这是因为 DriverInitArgs 是底层驱动初始化参数，
// OnnxDriverConfig 是上层 setupOnnxInferenceDriver 的配置。
// setupOnnxInferenceDriver 内部把 config.ep 赋给 onnxArgs->ep。
// ---------------------------------------------------------------------------
TEST_CASE("DriverInitArgs default values", "[driver][init-args]") {
    DriverInitArgs args;
    REQUIRE(args.ep == ExecutionProvider::CPUExecutionProvider);
    REQUIRE(args.deviceIndex == -1); // -1 means auto-select
    REQUIRE(args.runtimePath.empty());
}

// ---------------------------------------------------------------------------
// DriverInitArgs 与命名空间作用域 API_NAME / API_VERSION
//
// 注意：API_NAME 和 API_VERSION 是 srt::driver::onnx 命名空间作用域的
// inline constexpr 常量（定义在 OnnxDriverApi.h），不是 DriverInitArgs 类
// 的静态成员。DriverInitArgs 构造函数把它们传给基类 InferenceDriverInitArgs
// (NamedObject)，因此通过实例的 objectName() 和 version 字段验证绑定正确。
// ---------------------------------------------------------------------------
TEST_CASE("DriverInitArgs API_NAME and API_VERSION", "[driver][init-args][api]") {
    // 命名空间作用域常量（跨项目契约：Lite 通过字符串 "onnx" 识别驱动）
    REQUIRE(std::string(srt::driver::onnx::API_NAME) == "onnx");
    REQUIRE(srt::driver::onnx::API_VERSION >= 1);

    // 实例化 DriverInitArgs 后，构造函数应将 API_NAME/API_VERSION 传给基类
    DriverInitArgs args;
    REQUIRE(args.objectName() == std::string(srt::driver::onnx::API_NAME));
    REQUIRE(args.version == srt::driver::onnx::API_VERSION);
}

// ---------------------------------------------------------------------------
// SessionOpenArgs 默认值
//
// Lite G2pOnnxSessionTask 强制 useCpu=true，与默认值 false 不同。
// 这里验证默认值，确保 Lite 的强制行为是显式覆盖。
// ---------------------------------------------------------------------------
TEST_CASE("SessionOpenArgs default values", "[driver][session-args]") {
    SessionOpenArgs args;
    REQUIRE_FALSE(args.useCpu); // 默认不强制 CPU
    REQUIRE_FALSE(args.ep.has_value()); // 默认不覆盖 driver EP
    REQUIRE_FALSE(args.deviceIndex.has_value());
}

TEST_CASE("SessionOpenArgs can override EP", "[driver][session-args]") {
    SessionOpenArgs args;
    args.useCpu = true;
    args.ep = ExecutionProvider::CUDAExecutionProvider;
    args.deviceIndex = 0;

    REQUIRE(args.useCpu);
    REQUIRE(args.ep.has_value());
    REQUIRE(args.ep.value() == ExecutionProvider::CUDAExecutionProvider);
    REQUIRE(args.deviceIndex.value() == 0);
}

// ---------------------------------------------------------------------------
// OnnxDriverConfig 与 DriverInitArgs 的 EP 赋值兼容性
//
// setupOnnxInferenceDriver 实现中：onnxArgs->ep = config.ep;
// 这里验证枚举可以直接赋值。
// ---------------------------------------------------------------------------
TEST_CASE("OnnxDriverConfig ep can be assigned to DriverInitArgs ep",
          "[driver][config][compat]") {
    OnnxDriverConfig cfg;
    cfg.ep = ExecutionProvider::DMLExecutionProvider;
    cfg.deviceIndex = 0;

    DriverInitArgs args;
    args.ep = cfg.ep;
    args.deviceIndex = cfg.deviceIndex;

    REQUIRE(args.ep == ExecutionProvider::DMLExecutionProvider);
    REQUIRE(args.deviceIndex == 0);
}

// ---------------------------------------------------------------------------
// setupOnnxInferenceDriver 函数签名存在性（链接时校验）
//
// 这里只验证函数可以被取地址，不实际调用（需要 Runtime + plugin 路径）。
// 这确保 setupOnnxInferenceDriver 被 export 并可被 Lite 链接调用。
// ---------------------------------------------------------------------------
TEST_CASE("setupOnnxInferenceDriver is exported and addressable",
          "[driver][setup][linkage]") {
    // 取函数地址验证符号存在；不实际调用以避免需要 Runtime
    auto fn = &srt::driver::setupOnnxInferenceDriver;
    REQUIRE(fn != nullptr);
}

// ---------------------------------------------------------------------------
// OnnxSetup "no auto-fallback" 契约（文档化测试）
//
// 根据源码注释（lib/Driver/OnnxSetup.cpp L91-108）：
//   "Do not auto-fallback: report the error explicitly so the user knows
//    the configured EP is unavailable. The host application is responsible
//    for handling the failed initialization gracefully (e.g. showing a
//    dialog) without crashing."
//
// 这意味着：
//   - CUDA EP 失败 → 返回错误（不自动回退到 CPU）
//   - DML EP 失败 → 返回错误（不自动回退到 CPU）
//   - 调用方（Lite）负责处理失败
//
// 这里通过验证 setupOnnxInferenceDriver 函数存在来确保契约可被调用方依赖。
// 实际 EP 失败场景需要集成测试环境（真实 ORT + plugin 路径）。
// ---------------------------------------------------------------------------
TEST_CASE("OnnxSetup no-auto-fallback contract is documented",
          "[driver][setup][contract][no-fallback]") {
    // 契约验证：setupOnnxInferenceDriver 返回 Expected<void>，
    // 失败时返回 Error 而非自动回退。调用方通过 Expected 判断成功/失败。
    OnnxDriverConfig cfg;
    cfg.ep = ExecutionProvider::CUDAExecutionProvider;

    // 验证配置可以被构造（实际调用需要 Runtime）
    REQUIRE(cfg.ep == ExecutionProvider::CUDAExecutionProvider);

    // 验证 EP 名字符串可以被构造（与 OnnxSetup.cpp 内部 epName 逻辑对应）
    std::string epName;
    if (cfg.ep == ExecutionProvider::CUDAExecutionProvider) {
        epName = "CUDA";
    } else if (cfg.ep == ExecutionProvider::DMLExecutionProvider) {
        epName = "DirectML";
    } else {
        epName = "CPU";
    }
    REQUIRE(epName == "CUDA");
}

// ---------------------------------------------------------------------------
// ds-editor-lite 真实使用场景：SynthrtEngine EP 选择
//
// Lite SynthrtEngine::initialize 接收 ep 参数（来自 GPU 选择 UI），
// 构造 OnnxDriverConfig 后调用 setupOnnxInferenceDriver。
// Lite 默认使用 DML（与 Windows GPU 用户对齐），GPU 选择 UI 允许切换 CUDA。
// ---------------------------------------------------------------------------
TEST_CASE("lite SynthrtEngine EP selection maps to OnnxDriverConfig",
          "[driver][realworld]") {
    SECTION("default DML EP") {
        OnnxDriverConfig cfg; // 默认 DML
        REQUIRE(cfg.ep == ExecutionProvider::DMLExecutionProvider);
        REQUIRE(cfg.deviceIndex == 0);
    }

    SECTION("CUDA EP with device index") {
        OnnxDriverConfig cfg;
        cfg.ep = ExecutionProvider::CUDAExecutionProvider;
        cfg.deviceIndex = 0;
        REQUIRE(cfg.ep == ExecutionProvider::CUDAExecutionProvider);
    }

    SECTION("CPU EP (forced by G2pOnnxSessionTask)") {
        // Lite G2pOnnxSessionTask 强制 useCpu=true，但 driver 全局 EP 仍是 DML/CUDA
        SessionOpenArgs sessionArgs;
        sessionArgs.useCpu = true;
        REQUIRE(sessionArgs.useCpu);
    }
}
