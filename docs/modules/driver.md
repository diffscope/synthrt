# Driver 模块 (`srt::driver`)

namespace: `srt::driver` | target: `synthrt::driver` | 头文件: `include/synthrt/Driver/`

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
