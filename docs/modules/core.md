# Core 模块 (`srt::core`)

namespace: `srt::core` | target: `srt::core` | 头文件: `include/synthrt/Core/`

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
- `DisplayText` / `DisplayPath` — 多语言文本/路径（ds-spec 2.4 透传模型：键原样保留、Runtime 不做匹配）

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

### Runtime::loadPackage 部分失败回滚 (D-40)

`loadPackage` 内部按 `inferences → singers` 顺序解析每个 `ModuleSpec`，每个 spec 由 `parseSpec` 构造、`loadSpec(Initialized)` → `loadSpec(Ready)` 推进。D-40 修复前若第 N 个 spec 失败，前 N-1 个已 `loadSpec(Ready)` 的 spec 残留在 `ModuleCategory::Impl::modules` 列表（raw pointer，`std::list<ModuleSpec *>`）；重试同一包时 `loadSpecBase(Initialized)` 的 duplicate-detection 会对残留 spec 触发 `PackageDuplicate`，用户必须重启进程才能恢复。附带 bug：duplicate-detection 错误路径上 `parseSpec` 已构造但未 `loadSpec(Deleted)` 的 spec 未被 `delete`，造成内存泄漏。

D-40 修复方案（commit 93c0c92）：

```cpp
struct CommittedSpec { ModuleCategory *cat; ModuleSpec *spec; };
std::vector<CommittedSpec> committed;
auto rollbackCommitted = [&committed]() {
    for (auto it = committed.rbegin(); it != committed.rend(); ++it) {
        (void) it->cat->loadSpec(it->spec, ModuleSpec::Deleted);
        delete it->spec;
    }
    committed.clear();
};

for (const auto &ref : inferenceRefs) {
    auto spec = std::make_unique<InferenceSpec>(...);   // pending 由 unique_ptr 持有
    auto loadExp = infCat->loadSpec(spec.get(), ModuleSpec::Initialized);
    if (!loadExp) { rollbackCommitted(); return loadExp.takeError(); }
    loadExp = infCat->loadSpec(spec.get(), ModuleSpec::Ready);
    if (!loadExp) {
        infCat->loadSpec(spec.get(), ModuleSpec::Deleted);  // 移除 spec
        return loadExp.takeError();                          // unique_ptr 析构释放
    }
    committed.push_back({infCat, spec.get()});
    spec.release();   // 成功：移交所有权给 category
}
```

所有失败路径统一调用 `rollbackCommitted()` 逆序回滚，符合 ROBUST-05（禁止隐式错误吞没）、ROBUST-01（Expected 传播）、D-11（公共签名不变）、ARCH-02（未新增错误码）。

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
    DisplayText name() const;   // 多语言名称（全部翻译随对象携带），不再返回 std::string
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

`configurationDisplayName(const std::string &configKey)` 同样返回 `DisplayText`（配置声明文件中的多语言名；无声明时兜底为 `DisplayText(configKey)`）。

### DisplayText / DisplayPath（多语言文本）

```cpp
// include/synthrt/Core/Support/DisplayText.h
class DisplayText {
public:
    // 构造：纯字符串 → 仅默认文本
    DisplayText(std::string defaultText = {});

    // 严格解析多语言对象（ds-spec 2.4）：缺少 "_" 键或非对象 → Expected 报错
    static Expected<DisplayText> fromJsonValue(const JsonValue &value);

    // 容错解析：永不失败。缺 "_" 时按 "default" → "en" →
    // 首个字符串条目（按旧无 locale 键顺序）选定默认文本；非字符串条目跳过
    static DisplayText fromJsonValueTolerant(const JsonValue &value);

    const std::string &text() const;                   // 默认文本（"_" 键）
    const std::string *text(std::string_view key) const; // 按键直取；缺键返 nullptr
    stdc::array_view<std::string> locales() const;     // "_" 以外的全部键（原样、有序）
    bool isEmpty() const;
};

// include/synthrt/Core/Support/DisplayPath.h
class DisplayPath { /* 与 DisplayText 同契约的文件路径版本：path()/path(key)/locales() */ };
```

**透传语义（docs/ds-spec-2.4.md §多语言文本）**：

- 语言键是**不透明且区分大小写**的 map key（推荐 BCP 47，但 Runtime 不验证）；Runtime 不执行
  Lookup、大小写折叠、规范化或回退——POSIX 写法 `zh_CN` 只是一个普通键，与原样键名精确相等才命中。
- `text(key)` 是**纯查询**：键存在返回其值（值可为空串），不存在返回 `nullptr`；**不会**自动回退
  到 `_`。如何用偏好语言生成候选键序列、何时取 `text()`（`_`）、是否归并大小写，全部由前端决定；
  前端需要枚举时用 `locales()` 取得全部键的原样拼写。
- 全库唯一实现为 `srt::core::DisplayText`（原 `srt::g2p::DisplayText` 副本已删除）；
  `ModuleSpec::name()/configurationDisplayName()`、ds-bank 的所有人读字段均改用它
  （详见 ds-bank.md / ds-session.md）。
- 构造/解析路径：`fromJsonValue` 严格要求 `_`；`fromJsonValueTolerant` 缺 `_` 时按
  `default` → `en` → 首个字符串条目选定默认文本（解析容错，不是匹配）；构造函数会忽略
  翻译 map 里混入的 `_` 键。

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

所有可能失败的 API 返回 `Expected<T>`。错误系统基于分层 `ErrorCode` 枚举，按模块划分代码段（General 0-99, Package 100-199, Inference 200-299, G2P 300-399, Driver 400-499, S2P 500-599, SVS 600-699）。

### ErrorCode 枚举

```cpp
// include/synthrt/Core/Support/Diagnostic.h
enum class ErrorCode {
    None = 0, InvalidFormat, FileNotFound, ..., Unknown,           // General (0-99)
    PackageRootInvalid = 100, ..., PackageDuplicate,               // Package (100-199)
    InferenceNotInitialized = 200, ..., InferenceSampleRateMismatch, // Inference (200-299)
    G2pSuccess = 300, ..., G2pTaskNotFound,                        // G2P (300-399)
    DriverNotFound = 400, ..., DriverPluginNotFound,               // Driver (400-499)
    S2pResourceNotFound = 500, ..., S2pDictionaryError,            // S2P (500-599)
    SvsSingerNotFound = 600, ..., SvsCategoryNotFound,             // SVS (600-699)
};

enum class ErrorCategory { None, General, Package, Inference, G2P, Driver, S2P, SVS, Audio, Extract };

ErrorCategory errorCodeCategory(ErrorCode code) noexcept;
const char *errorCodeToString(ErrorCode code) noexcept;   // "Inference::ModelLoadFailed"
```

V3-09/V3-10 在 Inference 段追加 `LoadFailed` (217) / `RuntimePackageNotLoaded` (218)；在 G2P 段追加 `G2pVersionAmbiguous` (321)；V3-21 在 SVS 段追加 `SvsSingerAmbiguous` (601)。`Audio` (700-799) / `Extract` (800-899) 是 v4 新增的两个段。`ErrorCategory` 也对应追加 `Audio` / `Extract`，`errorCodeCategory` 使用范围检查（如 `>=600` => SVS），无需修改。

枚举值只追加不重排（ARCH-02），保证 ABI 稳定性。

### LevelCompatibilityChecker::ValidationResult 嵌套类型导出 (D-45)

```cpp
// include/synthrt/Core/Dependency/LevelCompatibilityChecker.h
class SRT_CORE_EXPORT LevelCompatibilityChecker {
public:
    struct LevelConfig { ... };

    // D-45：嵌套 struct 必须独立标记 SRT_CORE_EXPORT
    struct SRT_CORE_EXPORT ValidationResult {
        bool isCompatible;
        int pluginLevel, systemMinimum, systemMaximum;
        std::string message, suggestion;
        bool isInSupportedRange() const;
    };

    static ValidationResult checkCorePlugin(int pluginLevel, const LevelConfig &config);
    // ...
};
```

MSVC 的 `dllexport` 语义与 GCC/Clang visibility 不同：外层类标记 `SRT_CORE_EXPORT` 只导出外层类自身的成员，**不传播到嵌套类型的成员**（GCC/Clang 的 visibility 会传播）。D-45 修复前 `test_dependency_graph_extreme.cpp` 链接失败：LNK2019 未解析 `LevelCompatibilityChecker::ValidationResult::isInSupportedRange`。修复方案是给嵌套 struct 显式添加 `SRT_CORE_EXPORT`（commit c5e14a0）。这一约束对 GCC/Clang 是 no-op（已可见的类型再标记 visibility 属性不会改变行为），但对 MSVC 是强制的。所有跨 DLL 边界使用的嵌套类型必须显式标记 `SRT_CORE_EXPORT`（INFRA-01）。

### Error 类

```cpp
// include/synthrt/Core/Support/Error.h
class Error {
public:
    // 构造函数 — 自动捕获 std::source_location (C++20)
    Error(ErrorCode code, std::string msg,
          const std::source_location &loc = std::source_location::current());
    Error(ErrorCode code, std::string msg, Diagnostic context,
          const std::source_location &loc = std::source_location::current());

    // 查询方法
    ErrorCode code() const noexcept;
    ErrorCategory category() const noexcept;
    const char *codeString() const noexcept;     // "Inference::ModelLoadFailed"
    std::string message() const;
    std::string sourceLocation() const;          // "ModelSet.cpp:42:load"
    std::string toString() const;                // 完整错误描述
    bool ok() const noexcept;
    const Diagnostic &diagnostic() const;

    // trace 追加（跨层传播）
    void appendTrace(const std::source_location &loc = std::source_location::current(),
                     std::string note = {});
    void appendTrace(std::string entry);

    // 工厂函数（按模块分组，自动填充上下文）
    static Error packageError(ErrorCode code, std::string msg, std::string packageId = {}, ...);
    static Error inferenceError(ErrorCode code, std::string msg, std::string singerId = {},
                                std::string stage = {}, ...);
    static Error g2pError(ErrorCode code, std::string msg, std::string language = {},
                          std::string packageId = {}, ...);
};
```

`toString()` 输出格式：
```
[Inference::ModelLoadFailed] failed to load duration model
  at ModelSet.cpp:42:load
  singerId: "singer1", moduleId: "duration"
  trace:
    - InferenceService::run [InferenceService.cpp:85]
```

### Expected\<T\> 便捷方法

```cpp
// include/synthrt/Core/Support/Expected.h
template <class T>
class Expected {
public:
    // 原有 API
    bool hasValue() const noexcept;
    explicit operator bool() const noexcept;
    T &value();
    const T &value() const;
    T take();
    Error takeError();
    const Error &error() const;

    // v4 新增便捷方法
    std::string errorMessage() const;       // error().message()
    std::string errorString() const;        // error().toString()
    ErrorCode errorCode() const;            // error().code()
    ErrorCategory errorCategory() const;    // error().category()
    bool isError(ErrorCode code) const;     // error().code() == code
};
```

### 使用示例

```cpp
auto result = runtime.loadPackage(path);
if (!result) {
    // 一行看到完整错误
    qCritical() << QString::fromStdString(result.errorString());
    // 按错误码分支处理
    if (result.isError(ErrorCode::PackageManifestInvalid)) {
        // 提示声库描述文件损坏
    }
}
```
