# Driver 模块 (`srt::driver`)

namespace: `srt::driver` | target: `srt::driver` | 头文件: `include/synthrt/Driver/`

---

## 职责

Driver 模块提供 ONNX Runtime 推理后端的抽象：
- `InferenceDriver` — 推理驱动接口（创建 session）
- `InferenceSession` — 模型会话（open/close/run）
- `setupOnnxInferenceDriver()` — 便捷注册函数

---

## 关键 API

### setupOnnxInferenceDriver

```cpp
// include/synthrt/Driver/OnnxSetup.h
struct OnnxDriverConfig {
    srt::driver::onnx::ExecutionProvider ep =
        srt::driver::onnx::ExecutionProvider::DMLExecutionProvider;  // 默认 DML
    int deviceIndex = 0;
};

Expected<void> setupOnnxInferenceDriver(
    Runtime &runtime,
    const std::filesystem::path &pluginRoot,
    const OnnxDriverConfig &config);
```

**功能**: 
1. 注册插件路径（singer-provider / inference-driver / 5 个 inference-interpreters）
2. 加载 "onnx" InferenceDriverPlugin
3. 创建 OnnxDriver 并初始化
4. 注册到 Runtime 的 "inference" 类别，ID 为 "dsdriver"

**pluginRoot 约定**: 包含以下子目录的根：
- `srt-driver/inferencedrivers/`
- `dsinfer/singerproviders/`
- `dsinfer/inferenceinterpreters/`

### ExecutionProvider 枚举

```cpp
// include/synthrt/Driver/onnx/OnnxDriverApi.h
enum ExecutionProvider {
    CPUExecutionProvider = 0,
    CUDAExecutionProvider,
    DMLExecutionProvider,      // 注意：不是 DirectMLExecutionProvider
    CoreMLExecutionProvider,
};
```

### InferenceDriver 接口

```cpp
// include/synthrt/Driver/InferenceDriver.h
class InferenceDriver : public NamedObject {
public:
    virtual std::string arch() const = 0;
    virtual std::string backend() const = 0;
    virtual Expected<void> initialize(const NO<InferenceDriverInitArgs> &args) = 0;
    virtual NO<InferenceSession> createSession() = 0;
};
```

### InferenceSession 接口

```cpp
// include/synthrt/Driver/InferenceSession.h
class InferenceSession : public ITask {
public:
    virtual Expected<void> open(const std::filesystem::path &path,
                               const NO<InferenceSessionOpenArgs> &args) = 0;
    virtual Expected<void> close() = 0;
    virtual bool isOpen() const = 0;
    virtual int64_t id() const = 0;
};
```

`InferenceSession` 继承 `ITask`，支持 `start/stop/result` 生命周期。

### per-session EP 覆盖 (2026-07-11)

`SessionOpenArgs` 扩展了两个可选字段，使不同调用方（G2P、推理等）可以共享一个
`OnnxDriver` 实例，同时各自指定 EP：

```cpp
class SessionOpenArgs : public InferenceSessionOpenArgs {
public:
    bool useCpu = false;                              // 最高优先级：强制 CPU
    std::optional<ExecutionProvider> ep;               // per-session EP 覆盖
    std::optional<int> deviceIndex;                   // per-session device 覆盖
};
```

**EP 解析优先级**（在 `Session::open()` 中）：
1. `useCpu=true` → CPU（`SH_PreferCPUHint`，跳过 EP 初始化）
2. `args.ep` 已设置 → 用 `*args.ep` + `args.deviceIndex`（或全局 deviceIndex）
3. 都未设置 → 读全局 `Env::s_deviceConfig`（现有行为不变）

**缓存隔离**：解析后的 EP+deviceIndex 编码到 `SessionHint` 高位（bit 8-15 EP，
bit 16-23 deviceIndex+1），作为 `SessionImage` 缓存 key 的一部分，保证不同 EP
的模型镜像独立缓存。`SH_NoHint`（跟随全局）和 `SH_PreferCPUHint`（强制 CPU）
的 bit 布局不变，向后兼容。

**关键修复**：`SH_EPOverrideHint`（bit 1）作为哨兵标志，区分"显式传 ep=CPU"和
"跟随全局"。若不加此标志，`ep=CPU + deviceIndex=-1` 会编码为 0（= `SH_NoHint`），
命中全局配置的缓存（可能是 CUDA session），返回错误 EP。同时 `deviceIndex` 被
clamp 到 `[-1, 254]` 防止 8-bit 编码溢出。

**全局 EP 缓存失效修复**：else 分支（不传 ep，跟随全局）原本用 `SH_NoHint`(0)
作为缓存 key，不记录实际 EP。若运行时调用 `Env::setDeviceConfig()` 改变全局 EP，
后续 `open()` 仍命中旧 EP 的 SessionImage。现在全局 EP 也被编码进 hints，
`SH_NoHint` 退化为纯"未初始化"哨兵，不再作为缓存 key。

提交: `90834df`（特性）, `1a4adc2`（缓存 key 冲突修复）, `396cb43`（全局 EP 缓存失效修复）

---

## 调用关系

```
宿主层
  └── setupOnnxInferenceDriver(runtime, pluginRoot, config)
        ├── 注册插件路径到 Runtime
        ├── 加载 srt-onnxdriver 插件
        └── 创建 OnnxDriver → 注册为 "dsdriver"

推理插件 (acoustic/duration/...)
  └── getInferenceDriver(runtime)
        └── runtime.moduleCategory("inference")->getFirstObject("dsdriver")
              └── driver->createSession()
                    └── session->open(modelPath, args)
                          └── session->start(input) → session->result()
```

---

## ONNX 插件实现

`plugins/Driver/onnx/` 实现 `OnnxDriver` 和 `OnnxSession`：
- `OnnxDriver::createSession()` 创建 `OnnxSession`
- `OnnxSession::open()` 加载 ONNX 模型
- `OnnxSession::start()` 执行推理（`Ort::Session::Run()`）
- 异常边界：catch `Ort::Exception` + `std::exception`，转换为 `Error`
