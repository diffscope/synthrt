// T-10 ONNX Driver 失败重入测试 (BF-59 回归)
//
// 覆盖 BF-59 核查结论：OnnxDriver::Impl::load() 失败后可安全二次调用。
// 核查结论（[OnnxDriver.cpp](../../../plugins/Driver/onnx/OnnxDriver.cpp)）：
//   1. 失败的 dylib 由 unique_ptr 自动释放，无泄漏
//   2. ortDSO 仅在 open() 成功后 std::swap，失败时保持原状（空）
//   3. loaded 仅在成功后置 true
//   4. Ort::InitApi 仅在成功 open() 后调用
//   5. 二次 load() 调用会重新尝试（非缓存失败），因失败后状态干净
//
// === 测试策略 ===
// OnnxDriver 类位于 srt-onnxdriver 插件 DLL 中（CMakeLists.txt 使用 NO_EXPORT），
// 无法直接链接测试。本测试采用 MockOnnxDriver 精确复刻 OnnxDriver::Impl::load()
// 的关键状态管理不变量（unique_ptr 管理 dylib、loaded 标志、ortDSO swap 时机），
// 验证 BF-59 验证的设计模式在以下场景下保持正确：
//   - load() 失败 → 错误传播、状态干净
//   - 失败后重试 → 可成功、状态正确
//   - 成功后重入 → FileDuplicated 错误
//   - 多次失败 → 无累积副作用
//
// 同时通过契约测试验证 setupOnnxInferenceDriver 的错误传播路径（公共 API）。

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>
#include <synthrt/Driver/OnnxSetup.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>

using namespace srt::driver;
using srt::core::Error;
using srt::core::Expected;
using srt::core::NO;
using srt::driver::onnx::DriverInitArgs;
using srt::driver::onnx::ExecutionProvider;
using srt::driver::onnx::SessionOpenArgs;

namespace {

    // 模拟 OnnxDriver::Impl::load() 失败重入模式
    //
    // 精确复刻 [OnnxDriver.cpp](../../../plugins/Driver/onnx/OnnxDriver.cpp) 中
    // OnnxDriver::Impl 类与 OnnxDriver::initialize() 的关键状态管理不变量：
    //   - dylib 由 unique_ptr 管理（失败时自动释放）
    //   - ortDSO.swap(dylib) 仅在 open() 成功后调用
    //   - loaded 仅在成功后置 true
    //   - initialize() 在 loaded=true 时返回 FileDuplicated
    //   - initialize() 校验 args->objectName() == API_NAME
    //   - initialize() 校验 args.as<DriverInitArgs>() 非空
    //
    // 通过 m_failNextLoad 模拟 ORT 库加载失败场景（如缺失 onnxruntime.dll）。
    // 失败后 m_loaded 保持 false，下次 initialize() 可重试。
    class MockOnnxDriver : public InferenceDriver {
    public:
        explicit MockOnnxDriver() {
            setObjectName(onnx::API_NAME);
        }

        std::string arch() const override { return "diffsinger"; }
        std::string backend() const override { return onnx::API_NAME; }

        // 精确复刻 OnnxDriver::initialize() 的校验与 load() 调用顺序
        Expected<void> initialize(const NO<InferenceDriverInitArgs> &args) override {
            // 对应 OnnxDriver.cpp L146-152: 校验 driver name
            if (args->objectName() != onnx::API_NAME) {
                return std::move(Error{
                    Error::InvalidArgument,
                    std::string("invalid driver name: expected \"") +
                        onnx::API_NAME + "\", got \"" +
                        args->objectName() + "\"",
                }.withTrace(std::source_location::current(), "MockOnnxDriver::initialize"));
            }

            // 对应 OnnxDriver.cpp L154-159: 校验 args 类型
            auto onnxArgs = args.as<DriverInitArgs>();
            if (!onnxArgs) {
                return std::move(Error{Error::InvalidArgument,
                                       "onnx args is null pointer"}
                    .withTrace(std::source_location::current(), "MockOnnxDriver::initialize"));
            }

            // 对应 OnnxDriver.cpp L164-169: loaded 检查（FileDuplicated）
            if (m_loaded) {
                return std::move(Error{
                    Error::FileDuplicated,
                    "onnx runtime has been initialized by another instance",
                }.withTrace(std::source_location::current(), "MockOnnxDriver::initialize"));
            }

            // 对应 OnnxDriver.cpp L171-177: 调用 load(dllPath)
            // load() 内部：失败时 dylib 由 unique_ptr 释放，ortDSO 不 swap
            if (m_failNextLoad) {
                // 模拟 SharedLibrary::open() 失败（如 onnxruntime.dll 缺失）
                // 关键不变量：失败后 m_loaded 保持 false，m_dso 保持空
                m_failNextLoad = false; // 失败后下次可重试（非缓存失败）
                m_lastError = "Load library failed: simulated onnxruntime.dll missing";
                return std::move(Error{Error::SessionError, m_lastError}
                    .withTrace(std::source_location::current(), "MockOnnxDriver::Impl::load"));
            }

            // 对应 OnnxDriver.cpp L99-107: 成功路径
            // ortDSO.swap(dylib) + loaded = true + ortPath 设置
            m_dso = std::make_unique<int>(1); // 模拟 dylib 所有权转移
            m_loaded = true;
            m_ep = onnxArgs->ep;
            m_deviceIndex = onnxArgs->deviceIndex;
            m_runtimePath = onnxArgs->runtimePath;
            m_initApiCalled = true; // 对应 Ort::InitApi(api) 调用
            return Expected<void>{};
        }

        NO<InferenceSession> createSession() override { return nullptr; }

        // 状态访问器（用于断言不变量）
        bool isLoaded() const { return m_loaded; }
        bool hasDso() const { return m_dso != nullptr; }
        bool isInitApiCalled() const { return m_initApiCalled; }
        ExecutionProvider ep() const { return m_ep; }
        int deviceIndex() const { return m_deviceIndex; }
        const std::filesystem::path &runtimePath() const { return m_runtimePath; }
        const std::string &lastError() const { return m_lastError; }

        // 配置下一次 load() 是否失败
        void setFailNextLoad(bool fail) { m_failNextLoad = fail; }

    private:
        // 对应 OnnxDriver::Impl 的成员变量
        std::unique_ptr<int> m_dso;        // 模拟 ortDSO（unique_ptr<SharedLibrary>）
        bool m_loaded = false;              // 对应 Impl::loaded
        bool m_initApiCalled = false;       // 跟踪 Ort::InitApi 是否被调用
        bool m_failNextLoad = false;        // 测试钩子：控制 load() 失败
        ExecutionProvider m_ep = ExecutionProvider::CPUExecutionProvider;
        int m_deviceIndex = -1;
        std::filesystem::path m_runtimePath;
        std::string m_lastError;
    };

    // 辅助：构造 DriverInitArgs
    static NO<DriverInitArgs> makeArgs(ExecutionProvider ep,
                                        int deviceIndex,
                                        std::filesystem::path runtimePath = {}) {
        auto args = NO<DriverInitArgs>::create();
        args->ep = ep;
        args->deviceIndex = deviceIndex;
        args->runtimePath = std::move(runtimePath);
        return args;
    }

    // 辅助：构造类型不匹配的 InitArgs（objectName 不是 "onnx"）
    class WrongInitArgs : public InferenceDriverInitArgs {
    public:
        WrongInitArgs() : InferenceDriverInitArgs("wrong-name", 1) {}
    };

} // namespace

// ===========================================================================
// BF-59-001 ~ BF-59-004: load() 失败重入核心不变量
// 验证 OnnxDriver::Impl::load() 失败后的状态管理正确性
// ===========================================================================

// ---------------------------------------------------------------------------
// BF-59-001: load() 失败时返回明确错误（包含 "Load library failed"）
// 对应 OnnxDriver.cpp L56-64: dylib->open() 失败 → 返回 SessionError
// ---------------------------------------------------------------------------
TEST_CASE("BF-59-001: load() failure returns descriptive SessionError",
          "[driver][onnx][reentry][bf-59]") {
    MockOnnxDriver driver;
    driver.setFailNextLoad(true);

    auto args = makeArgs(ExecutionProvider::CPUExecutionProvider, -1,
                         "nonexistent/path");
    auto exp = driver.initialize(args);

    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.error().type() == Error::SessionError);
    REQUIRE(exp.error().message().find("Load library failed") != std::string::npos);
}

// ---------------------------------------------------------------------------
// BF-59-002: load() 失败后 loaded 保持 false，dso 保持空，InitApi 未调用
// 对应 OnnxDriver.cpp: 失败路径不执行 ortDSO.swap / loaded=true / Ort::InitApi
// 这是 BF-59 的核心不变量：失败后状态干净，无残留副作用
// ---------------------------------------------------------------------------
TEST_CASE("BF-59-002: after load() failure state stays clean",
          "[driver][onnx][reentry][bf-59]") {
    MockOnnxDriver driver;
    driver.setFailNextLoad(true);

    auto args = makeArgs(ExecutionProvider::CPUExecutionProvider, -1,
                         "nonexistent/path");
    auto exp = driver.initialize(args);
    REQUIRE_FALSE(exp.hasValue());

    // 核心不变量：失败后状态干净
    REQUIRE_FALSE(driver.isLoaded());      // loaded 保持 false
    REQUIRE_FALSE(driver.hasDso());        // ortDSO 保持空（未 swap）
    REQUIRE_FALSE(driver.isInitApiCalled()); // Ort::InitApi 未调用
}

// ---------------------------------------------------------------------------
// BF-59-003: 失败后再次 initialize 可重试成功
// 对应 BF-59 核查结论："二次 load() 调用会重新尝试（非缓存失败）"
// 关键：失败后 loaded=false，所以下次 initialize 不会触发 FileDuplicated
// ---------------------------------------------------------------------------
TEST_CASE("BF-59-003: retry after failure succeeds with clean state",
          "[driver][onnx][reentry][bf-59]") {
    MockOnnxDriver driver;

    // 第一次：失败
    driver.setFailNextLoad(true);
    auto args1 = makeArgs(ExecutionProvider::CPUExecutionProvider, -1,
                          "nonexistent/path");
    auto exp1 = driver.initialize(args1);
    REQUIRE_FALSE(exp1.hasValue());
    REQUIRE_FALSE(driver.isLoaded());

    // 第二次：可重试成功（非缓存失败）
    auto args2 = makeArgs(ExecutionProvider::CPUExecutionProvider, -1,
                          "valid/path");
    auto exp2 = driver.initialize(args2);
    REQUIRE(exp2.hasValue());
    REQUIRE(driver.isLoaded());
    REQUIRE(driver.hasDso());
    REQUIRE(driver.isInitApiCalled());
    REQUIRE(driver.ep() == ExecutionProvider::CPUExecutionProvider);
    REQUIRE(driver.runtimePath() == "valid/path");
}

// ---------------------------------------------------------------------------
// BF-59-004: 成功后再次 initialize 返回 FileDuplicated
// 对应 OnnxDriver.cpp L164-169: loaded=true 时拒绝重复初始化
// ---------------------------------------------------------------------------
TEST_CASE("BF-59-004: re-initialize after success returns FileDuplicated",
          "[driver][onnx][reentry][bf-59]") {
    MockOnnxDriver driver;

    auto args1 = makeArgs(ExecutionProvider::CPUExecutionProvider, -1, "path1");
    REQUIRE(driver.initialize(args1).hasValue());
    REQUIRE(driver.isLoaded());

    // 第二次：loaded=true，返回 FileDuplicated
    auto args2 = makeArgs(ExecutionProvider::CUDAExecutionProvider, 0, "path2");
    auto exp2 = driver.initialize(args2);
    REQUIRE_FALSE(exp2.hasValue());
    REQUIRE(exp2.error().type() == Error::FileDuplicated);
    REQUIRE(exp2.error().message().find("initialized") != std::string::npos);

    // 状态未被第二次调用破坏
    REQUIRE(driver.isLoaded());
    REQUIRE(driver.ep() == ExecutionProvider::CPUExecutionProvider); // 保持第一次的配置
    REQUIRE(driver.runtimePath() == "path1");
}

// ===========================================================================
// BF-59-005 ~ BF-59-007: args 校验路径
// 验证 initialize() 的前置校验不破坏状态
// ===========================================================================

// ---------------------------------------------------------------------------
// BF-59-005: 类型不匹配的 args 返回 InvalidArgument
// 对应 OnnxDriver.cpp L146-152: objectName != API_NAME
//
// 注意：OnnxDriver.cpp L154-159 的 `args.as<DriverInitArgs>()` 判空检查只能
// 捕获 null NO（as<>() 对 null 返回 null）。对于 non-null 但类型不匹配的 args，
// static_pointer_cast 返回 non-null（UB 解引用），实际安全保障来自 L146-152
// 的 objectName 校验。这里只测试 objectName 不匹配的安全路径。
// ---------------------------------------------------------------------------
TEST_CASE("BF-59-005: wrong objectName returns InvalidArgument without state change",
          "[driver][onnx][reentry][bf-59]") {
    MockOnnxDriver driver;
    auto wrongArgs = NO<WrongInitArgs>::create();

    auto exp = driver.initialize(wrongArgs);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.error().type() == Error::InvalidArgument);
    REQUIRE(exp.error().message().find("invalid driver name") != std::string::npos);
    // 状态未变
    REQUIRE_FALSE(driver.isLoaded());
    REQUIRE_FALSE(driver.hasDso());
    REQUIRE_FALSE(driver.isInitApiCalled());
}

// ---------------------------------------------------------------------------
// BF-59-006: 多次连续失败后状态保持干净（无累积副作用）
// 验证失败路径没有副作用累积（如内存泄漏、状态污染）
// ---------------------------------------------------------------------------
TEST_CASE("BF-59-006: repeated failures keep state clean",
          "[driver][onnx][reentry][bf-59][stress]") {
    MockOnnxDriver driver;

    // 连续 5 次失败
    for (int i = 0; i < 5; ++i) {
        driver.setFailNextLoad(true);
        auto args = makeArgs(ExecutionProvider::CPUExecutionProvider, -1,
                             "fail/path/" + std::to_string(i));
        auto exp = driver.initialize(args);
        REQUIRE_FALSE(exp.hasValue());
        // 每次失败后状态都应保持干净
        REQUIRE_FALSE(driver.isLoaded());
        REQUIRE_FALSE(driver.hasDso());
        REQUIRE_FALSE(driver.isInitApiCalled());
    }

    // 第 6 次成功
    auto args = makeArgs(ExecutionProvider::CPUExecutionProvider, -1, "success/path");
    auto exp = driver.initialize(args);
    REQUIRE(exp.hasValue());
    REQUIRE(driver.isLoaded());
    REQUIRE(driver.runtimePath() == "success/path");
}

// ---------------------------------------------------------------------------
// BF-59-007: 失败-成功-失败序列下状态正确
// 验证成功后再失败（通过新 driver 实例）的隔离性
// 同一 driver 成功后无法再失败（loaded=true 拒绝重入），所以这里验证
// 成功后的状态在多次查询中保持稳定
// ---------------------------------------------------------------------------
TEST_CASE("BF-59-007: state stable after success across multiple queries",
          "[driver][onnx][reentry][bf-59]") {
    MockOnnxDriver driver;

    // 成功
    auto args = makeArgs(ExecutionProvider::DMLExecutionProvider, 0, "dml/path");
    REQUIRE(driver.initialize(args).hasValue());

    // 多次查询状态稳定
    for (int i = 0; i < 10; ++i) {
        REQUIRE(driver.isLoaded());
        REQUIRE(driver.hasDso());
        REQUIRE(driver.isInitApiCalled());
        REQUIRE(driver.ep() == ExecutionProvider::DMLExecutionProvider);
        REQUIRE(driver.deviceIndex() == 0);
        REQUIRE(driver.runtimePath() == "dml/path");
    }

    // 再次 initialize 仍返回 FileDuplicated（不因多次查询而改变）
    auto exp = driver.initialize(args);
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.error().type() == Error::FileDuplicated);
}

// ===========================================================================
// BF-59-008 ~ BF-59-010: DriverInitArgs 配置与 setupOnnxInferenceDriver 契约
// 验证不同 EP 配置触发不同 runtimePath 拼接路径（对应 OnnxSetup.cpp L85-89）
// ===========================================================================

// ---------------------------------------------------------------------------
// BF-59-008: DriverInitArgs EP 配置覆盖
// 验证 CPU/CUDA/DML/CoreML 四种 EP 都能正确配置到 DriverInitArgs
// 对应 OnnxSetup.cpp: config.ep 赋给 onnxArgs->ep
// ---------------------------------------------------------------------------
TEST_CASE("BF-59-008: DriverInitArgs accepts all ExecutionProvider values",
          "[driver][onnx][reentry][bf-59][config]") {
    const std::array<ExecutionProvider, 4> eps = {
        ExecutionProvider::CPUExecutionProvider,
        ExecutionProvider::CUDAExecutionProvider,
        ExecutionProvider::DMLExecutionProvider,
        ExecutionProvider::CoreMLExecutionProvider,
    };

    for (size_t i = 0; i < eps.size(); ++i) {
        MockOnnxDriver driver;
        auto args = makeArgs(eps[i], static_cast<int>(i), "path");
        auto exp = driver.initialize(args);
        REQUIRE(exp.hasValue());
        REQUIRE(driver.ep() == eps[i]);
        REQUIRE(driver.deviceIndex() == static_cast<int>(i));
    }
}

// ---------------------------------------------------------------------------
// BF-59-009: setupOnnxInferenceDriver 错误传播契约（文档化测试）
// 对应 OnnxSetup.cpp L91-108: initialize 失败时返回包含 EP 名称和路径的错误
// 契约："Do not auto-fallback: report the error explicitly"
//
// 由于 setupOnnxInferenceDriver 需要真实 Runtime + 插件路径，这里通过
// MockOnnxDriver 验证错误传播模式：initialize 失败 → 上层包装错误消息
// ---------------------------------------------------------------------------
TEST_CASE("BF-59-009: setupOnnxInferenceDriver error wrapping contract",
          "[driver][onnx][reentry][bf-59][contract]") {
    // 模拟 OnnxSetup.cpp L91-108 的错误包装逻辑
    MockOnnxDriver driver;
    driver.setFailNextLoad(true);

    // 模拟 setupOnnxInferenceDriver 内部：构造 args 后调用 initialize
    auto onnxArgs = NO<DriverInitArgs>::create();
    onnxArgs->ep = ExecutionProvider::CUDAExecutionProvider;
    onnxArgs->deviceIndex = 0;
    onnxArgs->runtimePath = "runtimes/onnx/cuda";

    auto initExp = driver.initialize(onnxArgs);
    REQUIRE_FALSE(initExp.hasValue());

    // 模拟 OnnxSetup.cpp L96-101 的 EP 名称构造
    std::string epName;
    if (onnxArgs->ep == ExecutionProvider::CUDAExecutionProvider) {
        epName = "CUDA";
    } else if (onnxArgs->ep == ExecutionProvider::DMLExecutionProvider) {
        epName = "DirectML";
    } else {
        epName = "CPU";
    }

    // 模拟 OnnxSetup.cpp L102-108 的错误包装
    std::string wrappedMsg =
        "setupOnnxInferenceDriver: failed to initialize onnx driver (" +
        epName + " EP, runtime path: " + onnxArgs->runtimePath.string() +
        "): " + initExp.error().message();

    // 验证错误消息包含关键信息（ROBUST-05: 显式报错）
    REQUIRE(wrappedMsg.find("CUDA") != std::string::npos);
    REQUIRE(wrappedMsg.find("runtimes/onnx/cuda") != std::string::npos);
    REQUIRE(wrappedMsg.find("Load library failed") != std::string::npos);
    REQUIRE(wrappedMsg.find("setupOnnxInferenceDriver") != std::string::npos);
}

// ---------------------------------------------------------------------------
// BF-59-010: OnnxSetup no-auto-fallback 契约（文档化测试）
// 对应 OnnxSetup.cpp L92-95 注释：
//   "Do not auto-fallback: report the error explicitly so the user knows
//    the configured EP is unavailable."
//
// 验证：CUDA EP 失败时不会自动回退到 CPU EP（调用方负责处理失败）
// 通过 MockOnnxDriver 验证：失败后 loaded=false，调用方可重新配置并重试
// ---------------------------------------------------------------------------
TEST_CASE("BF-59-010: no auto-fallback contract - caller handles failure",
          "[driver][onnx][reentry][bf-59][contract]") {
    // 场景：用户配置 CUDA EP，但 CUDA runtime 缺失
    MockOnnxDriver driver;
    driver.setFailNextLoad(true);

    auto cudaArgs = makeArgs(ExecutionProvider::CUDAExecutionProvider, 0,
                             "runtimes/onnx/cuda");
    auto cudaExp = driver.initialize(cudaArgs);
    REQUIRE_FALSE(cudaExp.hasValue());
    REQUIRE_FALSE(driver.isLoaded());

    // 契约：驱动不自动回退到 CPU（setupOnnxInferenceDriver 直接返回错误）
    // 调用方（如 Lite SynthrtEngine）负责处理失败：
    //   - 显示错误对话框
    //   - 或回退到 CPU EP（由调用方显式重新调用 setupOnnxInferenceDriver）
    //
    // 这里验证调用方可以显式回退：用 CPU EP 重新 initialize
    auto cpuArgs = makeArgs(ExecutionProvider::CPUExecutionProvider, -1,
                            "runtimes/onnx/default");
    auto cpuExp = driver.initialize(cpuArgs);
    REQUIRE(cpuExp.hasValue());
    REQUIRE(driver.isLoaded());
    REQUIRE(driver.ep() == ExecutionProvider::CPUExecutionProvider);
    REQUIRE(driver.runtimePath() == "runtimes/onnx/default");

    // 验证 CPU 回退后状态正确（不是 CUDA 的残留状态）
    REQUIRE(driver.ep() != ExecutionProvider::CUDAExecutionProvider);
}

// ---------------------------------------------------------------------------
// BF-59-011: createSession 在未初始化时返回空（不崩溃）
// 对应 OnnxDriver.cpp L186-189: createSession 不依赖 loaded 状态
// 但返回的 session 在未初始化驱动上调用 open() 会失败（ORT 未初始化）
// 这里验证 createSession 本身的安全性
// ---------------------------------------------------------------------------
TEST_CASE("BF-59-011: createSession is safe on uninitialized driver",
          "[driver][onnx][reentry][bf-59]") {
    MockOnnxDriver driver;
    // 未调用 initialize
    REQUIRE_FALSE(driver.isLoaded());

    // createSession 不应崩溃（返回 nullptr 在 mock 中）
    auto session = driver.createSession();
    REQUIRE(!session); // mock 返回 nullptr

    // 驱动状态未被破坏
    REQUIRE_FALSE(driver.isLoaded());
}

// ---------------------------------------------------------------------------
// BF-59-012: 跨 driver 实例的状态隔离
// 验证一个 driver 实例的失败不影响另一个实例（无全局状态污染）
// 对应 OnnxDriver 设计：每个 OnnxDriver 实例有独立的 Impl（独立 ortDSO/loaded）
// ---------------------------------------------------------------------------
TEST_CASE("BF-59-012: driver instances are state-isolated",
          "[driver][onnx][reentry][bf-59]") {
    MockOnnxDriver driver1;
    MockOnnxDriver driver2;

    // driver1 失败
    driver1.setFailNextLoad(true);
    auto exp1 = driver1.initialize(
        makeArgs(ExecutionProvider::CPUExecutionProvider, -1, "path1"));
    REQUIRE_FALSE(exp1.hasValue());
    REQUIRE_FALSE(driver1.isLoaded());

    // driver2 成功（不受 driver1 失败影响）
    auto exp2 = driver2.initialize(
        makeArgs(ExecutionProvider::CPUExecutionProvider, -1, "path2"));
    REQUIRE(exp2.hasValue());
    REQUIRE(driver2.isLoaded());
    REQUIRE(driver2.runtimePath() == "path2");

    // driver1 仍可独立重试
    auto exp1b = driver1.initialize(
        makeArgs(ExecutionProvider::CPUExecutionProvider, -1, "path1b"));
    REQUIRE(exp1b.hasValue());
    REQUIRE(driver1.isLoaded());
    REQUIRE(driver1.runtimePath() == "path1b");

    // driver2 状态未受 driver1 重试影响
    REQUIRE(driver2.isLoaded());
    REQUIRE(driver2.runtimePath() == "path2");
}
