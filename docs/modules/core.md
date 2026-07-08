# Core 模块 (`srt::core`)

namespace: `srt::core` | target: `synthrt::core` | 头文件: `include/synthrt/Core/`

---

## 职责

Core 是 synthrt 的基础设施层，提供：
- `Runtime` — 运行时入口，管理插件注册、包加载、模块类别
- `NamedObject` / `NO<T>` — 命名对象基类与共享指针
- `ServiceRegistry` — 服务注册表
- `ModuleCategory` / `ModuleSpec` — 模块类别与规格
- `ITask` — 任务基类（推理任务的抽象）
- `Expected<T>` / `Error` / `Diagnostic` — 错误处理
- `DependencyGraph` / `VersionUtils` — 依赖解析与版本管理

---

## 关键 API

### Runtime

```cpp
// include/synthrt/Core/Core/Runtime.h
class Runtime {
public:
    Runtime();
    ~Runtime();

    // 服务注册表
    ServiceRegistry &services();

    // Stage 1: 包扫描（仅文件系统 + JSON，不加载 DLL）
    Expected<void> scanPackages(const std::filesystem::path &rootDir);
    std::vector<std::filesystem::path> discoveredPackages() const;

    // Stage 2: 初始化（一次性，幂等）
    Expected<void> initialize();
    bool isInitialized() const;

    // 模块类别查询
    ModuleCategory *moduleCategory(const std::string_view &name) const;

    // 包加载（解析 desc.json，创建并加载 InferenceSpec/SingerSpec）
    Expected<void> loadPackage(const std::filesystem::path &path);
};
```

**调用时机**:
1. `scanPackages()` — 可多次调用，仅扫描
2. `setupOnnxInferenceDriver()` — 注册 ONNX 驱动（Driver 模块提供）
3. `loadPackage()` — 加载声库包，创建 spec
4. `initialize()` — 一次性初始化（当前为 no-op Stage 2）

### ITask

```cpp
// include/synthrt/Core/Task/ITask.h
class ITask : public NamedObject {
public:
    enum State { Idle, Running, Failed, Terminated };

    using StartAsyncCallback =
        std::function<void(const NO<TaskResult> &, const Error &)>;

    virtual Expected<void> initialize(const NO<TaskInitArgs> &args);
    virtual Expected<NO<TaskResult>> start(const NO<TaskStartInput> &input) = 0;
    virtual Expected<void> startAsync(const NO<TaskStartInput> &input,
                                      const StartAsyncCallback &callback);
    virtual bool stop() = 0;

    State state() const;
    virtual NO<TaskResult> result() const = 0;

protected:
    void setState(State state);
};
```

`TaskResult` 携带 `Error error` 字段，推理失败时填充错误码和消息。

`Inference` 继承 `ITask`，推理插件实现 `start/stop/result`。`startAsync` 提供异步回调入口（默认实现返回错误，子类可覆盖）。`setState()` 为 protected，仅子类可调用以推进状态机。

### NO<T> 智能指针

```cpp
// include/synthrt/Core/Core/NamedObject.h
template <class T>
class NO : public std::shared_ptr<T> {
    static_assert(std::is_base_of<NamedObject, T>::value);
    // ...
};
```

`NO<T>` 是 `std::shared_ptr<T>` 的派生类，引用计数语义。可拷贝、可移动、可默认构造（空）。

### ModuleCategory / ModuleSpec

```cpp
// include/synthrt/Core/Module/Module.h
class ModuleCategory : public ObjectPool {
public:
    const std::string &name() const;
    Runtime *runtime() const;
    std::vector<ModuleSpec *> findSpec(const ModuleLocator &identifier) const;
    std::vector<ModuleSpec *> specs() const;
    template <class T> constexpr T *as();
};

class ModuleSpec {
public:
    enum State { Invalid, Initialized, Ready, Finished, Deleted };
    const std::string &id() const;
    const std::string &category() const;
    const std::string &className() const;
    std::string name() const;
    int apiLevel() const;
    const JsonObject &manifestConfiguration() const;
    NO<TaskConfiguration> configuration() const;
    const std::filesystem::path &path() const;
    State state() const;
    Runtime *runtime() const;
    ContextKey contextKey() const;
    template <class T> constexpr T *as();
};
```

`ModuleLocator` 由 `(package, version, id)` 三元组定位 ModuleSpec，任意子集可省略（匹配所有满足条件的 spec）。

`InferenceCategory` 和 `SingerCategory` 继承 `ModuleCategory`，`InferenceSpec`/`SingerSpec` 继承 `ModuleSpec`。`InferenceCategory::findInferences()` 和 `SingerCategory::findSingers()` 是类型安全的便捷封装，内部调用 `findSpec()`。

---

## 被谁调用

- **Driver**: `setupOnnxInferenceDriver(runtime, ...)` 注册驱动到 Runtime
- **SVS**: `InferenceCategory`/`SingerCategory` 注册为 ModuleCategory
- **G2P**: `LanguageService::initialize()` 不直接依赖 Runtime（G2P Manager 是进程单例）
- **ds-bank**: `VoicebankScanner` 独立于 Runtime（纯值类型）
- **ds-infer**: `SingerStageResolver::resolve(runtime, ...)` 查询 Runtime 的模块类别
- **C ABI**: `srt_session` 内部持有 Runtime 实例

---

## 错误处理

所有可能失败的 API 返回 `Expected<T>`。`Error` 包含错误码和消息。`Diagnostic` 包含错误码、严重级别和消息（用于更丰富的错误报告）。

```cpp
auto result = runtime.loadPackage(path);
if (!result) {
    auto &err = result.error();
    // err.code(), err.message()
}
```
