# G2P 模块 (`srt::g2p`)

namespace: `srt::g2p` (G2P 框架 + LanguageService) | target: `srt::g2p` | 头文件: `include/synthrt/G2P/`

---

## 职责

G2P (Grapheme-to-Phoneme) 模块负责歌词到音素的转换：
- `LanguageService` (`srt::g2p`) — 语言服务入口，初始化 G2P Manager、解析语言路由、批量转换、S2P 资源解析
- `Manager` (`srt::g2p`) — G2P 管理器（进程级单例）
- `PackageManager` — G2P 包管理（含版本感知 context 移除）
- G2P 插件 — chain/lstm/mandarin/cantonese/ds-dict

---

## 关键 API

### LanguageService

V3-01 / WP1 落地 5 层版本隔离后，`LanguageService` 拆出 Stage 1 (metadata) 与 Stage 2 (ONNX models) 两个初始化阶段，并对 `initializeMetadata` / `resolveLanguageRoute` / `resolveS2pResource` / `convert` 提供 **version-aware** 重载。Legacy 重载委托新重载并传空 version，单版本场景透明兼容；多版本同 packageId 场景下空 version 会触发 `G2pVersionAmbiguous`。

```cpp
// include/synthrt/G2P/LanguageService.h
namespace srt::g2p;

struct PackageDirectoryEntry {
    std::string packageId;
    stdc::VersionNumber version;
    std::filesystem::path path;
};

struct PackageDirectoryDiff {
    std::vector<PackageDirectoryEntry> added;
    std::vector<PackageDirectoryEntry> removed;
    std::vector<PackageDirectoryEntry> unchanged;
};

class LanguageService {
public:
    LanguageService();
    ~LanguageService();

    // === Stage 1: Metadata 初始化（快，无 ONNX）===
    // 新 version-aware 入口（V3-01）。按 (packageId, version) 注册 voicebank G2P context。
    Expected<void> initializeMetadata(
        const std::vector<std::filesystem::path> &pluginSearchPaths,
        const std::vector<std::filesystem::path> &officialG2pPackagePaths,
        const std::vector<PackageDirectoryEntry> &packageDirs);

    // 增量更新（V3-16 / WP8 热重载）。计算 diff、注册 added、通过
    // PackageManager::removeContextsByPrefix(prefix, version) 移除 removed、
    // 失效 manifest 与 S2P cache。要求 metadataReady()==true，禁止在
    // initializeModels() 之后调用。
    Expected<PackageDirectoryDiff> updateMetadata(
        const std::vector<std::filesystem::path> &pluginSearchPaths,
        const std::vector<std::filesystem::path> &officialG2pPackagePaths,
        const std::vector<PackageDirectoryEntry> &packageDirs);

    [[deprecated("Use the version-aware overload. Will be removed in Level=3.")]]
    Expected<void> initializeMetadata(
        const std::vector<std::filesystem::path> &pluginSearchPaths,
        const std::vector<std::filesystem::path> &officialG2pPackagePaths,
        const std::unordered_map<std::string, std::filesystem::path> &packageDirs);

    // === Stage 2: ONNX 模型加载（慢）===
    Expected<void> initializeModels();

    // === Convenience: Stage 1 + Stage 2 ===
    Expected<void> initialize(
        const std::vector<std::filesystem::path> &pluginSearchPaths,
        const std::vector<std::filesystem::path> &officialG2pPackagePaths,
        const std::unordered_map<std::string, std::filesystem::path> &packageDirs);

    bool metadataReady() const;   // Stage 1 完成
    bool modelsReady() const;     // Stage 2 完成
    bool ready() const;           // metadataReady() && modelsReady()

    // === Per-singer 路由（V3-01 version-aware）===
    Expected<LanguageRoute> resolveLanguageRoute(
        const std::string &packageId,
        const stdc::VersionNumber &version,
        const std::string &singerId,
        const std::string &languageId) const;

    [[deprecated("Use the version-aware overload. Will be removed in Level=3.")]]
    Expected<LanguageRoute> resolveLanguageRoute(
        const std::string &packageId,
        const std::string &singerId,
        const std::string &languageId) const;

    // === S2P 资源（V3-01 version-aware）===
    Expected<std::shared_ptr<srt::s2p::LanguageResource>> resolveS2pResource(
        const std::string &packageId,
        const stdc::VersionNumber &version,
        const std::string &singerId,
        const std::string &languageId) const;

    [[deprecated("Use the version-aware overload. Will be removed in Level=3.")]]
    Expected<std::shared_ptr<srt::s2p::LanguageResource>> resolveS2pResource(
        const std::string &packageId,
        const std::string &singerId,
        const std::string &languageId) const;

    // === 批量 G2P（要求 modelsReady）===
    std::vector<srt::g2p::G2pRes> convertLyric(
        const std::vector<srt::g2p::G2pInput> &input) const;

    // === 便捷转换（路由 + G2P，V3-01 version-aware）===
    Expected<std::vector<srt::g2p::G2pRes>> convert(
        const std::string &packageId,
        const stdc::VersionNumber &version,
        const std::string &singerId,
        const std::string &languageId,
        const std::vector<srt::g2p::G2pInput> &inputs) const;

    [[deprecated("Use the version-aware overload. Will be removed in Level=3.")]]
    Expected<std::vector<srt::g2p::G2pRes>> convert(
        const std::string &packageId,
        const std::string &singerId,
        const std::string &languageId,
        const std::vector<srt::g2p::G2pInput> &inputs) const;
};
```

### V3-01 五层版本隔离

| 层 | 隔离键 | 实现 |
|---|---|---|
| 1. entry | `(packageId, version)` | `PackageDirectoryEntry` 替代 legacy `unordered_map<packageId, path>` |
| 2. route | `(packageId, version, singerId, languageId)` | version-aware `resolveLanguageRoute` |
| 3. Manager context | `packageId__singerId` + voicebank version | `PackageManager::addPackagePath(context, version, path)` |
| 4. S2P cache | `(packageId, version, singerId, languageId)` | version-aware `resolveS2pResource` |
| 5. convert | `(packageId, version, singerId, languageId, inputs)` | version-aware `convert` |

context 命名约定（V3-02）：`packageId__singerId`，使用 `__` 而非 `:`（`ContextUtils::validateContextName` 禁用 `:`，保留给 FQID 分隔）。`g2pContextVersion` 是 voicebank 包版本，不是 G2P 子包 `g2pPackageVersion`（两者独立，可能冲突）。

### LanguageRoute

```cpp
// include/diffsinger/Lang/LanguageRoute.h  (注：实际位置在 include/synthrt/G2P/LanguageRoute.h)
namespace srt::g2p;

struct LanguageRoute {
    std::string g2pId;
    std::string singerId;
    stdc::VersionNumber g2pContextVersion;
    bool voicebankContext = false;

    std::string s2pMode;               // "dict" | "direct"
    std::filesystem::path s2pFile;
    std::filesystem::path onsetFile;
};
```

---

## 初始化流程（两阶段）

```cpp
Expected<void> LanguageService::initializeMetadata(
        pluginSearchPaths, officialG2pPackagePaths, packageDirs) {
    auto mgr = srt::g2p::Manager::instance();

    // Stage 1.1: 注册 G2P 插件搜索路径（仅首次）
    // Stage 1.2: 注册官方 G2P 包（仅首次）
    // Stage 1.3: 注册 voicebank 私有 G2P 包（始终执行，按 (packageId, version)）
    //            parsePackage → resolveG2pRoute → addPackagePath(context, version, path)
    //            失败记录 Diagnostic 但不中断（非默认上下文容错）
    // Stage 1.4: metadataReady() = true
}

Expected<void> LanguageService::initializeModels() {
    // Stage 2.1: 加载 G2P 插件 DLL
    // Stage 2.2: 创建 ONNX session、调用 Manager::initialize()
    //            失败时 G2P 转换被禁用但 route 解析仍可用
    // Stage 2.3: modelsReady() = true
}

Expected<void> LanguageService::initialize(...) {
    // 便捷包装：initializeMetadata() + initializeModels()
}
```

**关键约束**:
- G2P Manager 是进程级单例，多 LanguageService 实例共享
- Stage 1.3 始终执行（ER-08 修复），确保多会话场景下 voicebank G2P 正确注册
- ONNX 驱动 (`g2pOnnxDriver`) 必须在 `initialize()` 之前注册（D-20）
- `updateMetadata()` 在 `initializeModels()` 之后禁止调用（Manager context 不可变），调用方需重启进程
- **Manager::initialize() 允许空默认上下文**：当未注册任何官方 G2P 包时（voicebank-only 部署），Phase 1 跳过默认上下文初始化（不写入 `m_contextStates`），仅记录 `srtWarning`，继续执行 Phase 2 声库上下文初始化。尝试使用默认上下文会返回 `G2pContextNotFound`。这符合 ds-session.md §210："某 G2P module 初始化失败：仅关联 singer-language Disabled，其他语言/声库仍可发布"。

### updateMetadata 热重载 (V3-16 / WP8)

`updateMetadata(newPluginPaths, newOfficialPaths, newPackageDirs)` 计算 diff：

- `added` → `addPackagePath(context, version, path)` 注册新 context
- `removed` → `PackageManager::removeContextsByPrefix(prefix, version)` **版本感知**移除（D-43）
- `unchanged` → 不重新注册

`VoicebankSession::refresh()` 在扫描完成后调用 `updateMetadata()`，把退役 voicebank 的 G2P context 同步下线。若 `metadataReady()==false`，自动降级为完整 `initializeMetadata()` 调用（WP8-session 兜底）。

**插件路径 / 官方 G2P 路径的补注册**：当 `Manager::initialize()` 尚未执行（`modelsReady==false`）时，`updateMetadata()` 也会补注册 `pluginSearchPaths` 和 `officialG2pPackagePaths`（等价于 `initializeMetadata()` Stage 1.1/1.2，且 `addPluginPath` / `addPackagePath` 幂等）。这解决了 `PackageManager` 在 `SynthrtEngine::initialize()` 设置 `officialG2pPackages` 之前触发 `session.refresh()` 导致首次 `initializeMetadata()` 以空路径注册、`metadataReady=true` 后 `updateMetadata()` 无法补注册的问题。`Manager::initialize()` 执行后（`modelsReady==true`），热重载限制仍然生效：修改插件/官方 G2P 路径需重启进程。

---

## PackageManager 版本感知 context 移除 (D-43)

```cpp
// include/synthrt/G2P/Core/PackageManager.h
class PackageManager : public srt::core::PluginFactory {
public:
    // Legacy: 按前缀匹配，不区分 version（"matches at every version"）
    Expected<size_t> removeContextsByPrefix(const std::string &prefix);

    // V3-01 §2.4 / D-43：按 (prefix, version) 精确匹配
    // 多版本同 packageId 共存时，移除一个 version 不影响其他 version
    Expected<size_t> removeContextsByPrefix(
        const std::string &prefix, const stdc::VersionNumber &version);
};
```

匹配条件 `ctxKey.context.starts_with(prefix) && ctxKey.version == version`。空 `version` 仅匹配未版本化 context（通过 2-arg `addPackagePath` 注册的那些）；hot reload 路径下调用方应传具体退役 version。新重载清理 7 个 per-context map（`contextPackagePaths` / `contextStates` / `contextDependencyErrors` / `contextModuleInfos` / `contextDependencyResolved` / `contextDependencyGraphs` / `contextCachedIndexes`）以及 `tasks` map 中对应条目。

---

## G2P 插件

| 插件 | 目录 | 说明 |
|---|---|---|
| chain | `plugins/G2P/chain/` | 链式 G2P（多步骤管线：dict → model → format → validate → fallback） |
| lstm | `plugins/G2P/lstm/` | LSTM G2P（ONNX 模型） |
| mandarin | `plugins/G2P/mandarin/` | 普通话拼音 G2P |
| cantonese | `plugins/G2P/cantonese/` | 粤语 G2P |
| ds-dict | `plugins/G2P/ds-dict/` | DiffSinger 字典 G2P |

### Chain G2P 管线

`plugins/G2P/chain/internal/Core/G2pPipeline.cpp` 实现多步骤管线：

```
DictStep → ModelStep → FormatStep → TagAndValidateStep → FallbackStep
```

每个步骤实现 `G2pStep::process()`，失败时由 `FallbackStep` 兜底。`DirectS2P` 折叠连续空格并跳过空 token（容忍前导/连续/尾随空格，5e49112 修复）。

---

## 调用关系

```
宿主层 (VoicebankSession / Lite SynthrtEngine)
  ├── langSvc.initializeMetadata(pluginPaths, officialG2pPaths, packageDirs)
  │     └── Manager::instance()->addPackagePath(context, version, path)
  ├── langSvc.initializeModels()  [可选，懒加载]
  │     └── Manager::instance()->initialize()
  │
  ├── langSvc.updateMetadata(...)  [V3-16 热重载]
  │     ├── diff 计算 (added/removed/unchanged)
  │     └── mgr->removeContextsByPrefix(prefix, version)  [D-43]
  │
  ├── langSvc.resolveLanguageRoute(packageId, version, singerId, languageId)
  │     └── 返回 LanguageRoute (g2pId + g2pContextVersion + s2pFile + onsetFile)
  │
  ├── langSvc.resolveS2pResource(packageId, version, singerId, languageId)
  │     └── 返回 shared_ptr<LanguageResource>（缓存）
  │
  └── langSvc.convert(packageId, version, singerId, languageId, inputs)
        ├── resolveLanguageRoute()
        └── convertLyric(inputs)
              └── Manager::instance()->convert() → G2P 插件执行
```

---

## 与其他模块的协作

- **S2P**: `LanguageRoute.s2pFile` 指向 S2P 资源，`resolveS2pResource` 直接返回 `shared_ptr<LanguageResource>` 由宿主层调用 `convert()`
- **ds-bank**: `packageDirs` 来自 `VoicebankScanner::packageDirectories(packageId)`（V3-01 §1.6，返回 `vector<PackageDirectoryResult>{version, path}`，多版本同 packageId 全部保留）
- **ONNX Driver**: lstm 插件依赖 ONNX 驱动，必须在 `initialize()` 前注册（D-20）

---

## G2P 错误系统

`srt::g2p::Error` 继承 `srt::core::Error`，v4 起使用 `srt::core::ErrorCode`（G2p* 代码段 300-399）替代原有的 12 值 Type 枚举。旧 Type 枚举标记为 `[[deprecated]]` 但仍可用。

```cpp
// include/synthrt/G2P/Support/Error.h
class Error : public srt::core::Error {
public:
    using srt::core::ErrorCode;  // 引入 G2p* 代码

    Error(ErrorCode code, std::string msg,
          const std::source_location &loc = std::source_location::current());
    Error(ErrorCode code, std::string msg, std::string suggestion,
          const std::source_location &loc = std::source_location::current());

    bool ok() const noexcept override;   // 检查 ErrorCode::G2pSuccess

    [[deprecated]] enum Type { Success=0, ConfigError=1, ..., AlreadyInitialized=11 };
};
```

### G2P 错误码

| ErrorCode | 数值 | 说明 |
|---|---|---|
| `G2pSuccess` | 300 | 成功 |
| `G2pConfigError` | 301 | 配置错误 |
| `G2pFileSystemError` | 302 | 文件系统错误 |
| `G2pDependencyError` | 303 | 依赖错误 |
| `G2pRuntimeError` | 304 | 运行时错误 |
| `G2pNotImplementedError` | 305 | 未实现（如 session 未注入 LanguageService） |
| `G2pInitializationError` | 306 | Manager 初始化失败 |
| `G2pValidationError` | 307 | 音素校验失败 |
| `G2pNullPointerError` | 308 | 空指针 |
| `G2pIndexError` | 309 | 越界 |
| `G2pTimeoutError` | 310 | 超时 |
| `G2pAlreadyInitialized` | 311 | Manager 已初始化（`updateMetadata` 在 `initializeModels` 之后调用） |
| `G2pRouteNotFound` | 312 | 语言路由未找到 |
| `G2pPackageNotFound` | 313 | G2P 包未找到 |
| `G2pPluginNotFound` | 314 | 插件未找到 |
| `G2pDriverNotFound` | 315 | ONNX 驱动未找到 |
| `G2pDriverInitFailed` | 316 | 驱动初始化失败 |
| `G2pConversionFailed` | 317 | 转换失败 |
| `G2pSessionError` | 318 | Session 错误 |
| `G2pContextNotFound` | 319 | Context 未找到 |
| `G2pTaskNotFound` | 320 | Task 未找到 |
| `G2pVersionAmbiguous` | 321 | V3-09/V3-10：packageId 注册多版本但调用方未传 version |

错误创建使用 `Error::g2pError()` 工厂函数，自动填充 language/packageId 上下文和源位置。

### resolveLanguageRoute 错误码

| 场景 | ErrorCode |
|---|---|
| `(packageId, version)` 不在 packageDirs 中 | `G2pPackageNotFound` |
| version 为空 + 多个 packageId 匹配 | `G2pVersionAmbiguous`（列出所有候选 version/path） |
| singerId 在包中未找到 | `G2pRouteNotFound` |
| 语言未配置 G2P | `G2pRouteNotFound` |

### G2pRes candidates 兜底 (c100230)

`G2pRes` 构造函数在 lyric fallback 后必须先填充 `pronunciation`，再从 `pronunciation` 种子 `candidates`：

```cpp
G2pRes(std::string lyric, ..., std::string pronunciation = {},
       std::vector<std::string> candidates = {}, ...) {
    if (this->pronunciation.empty())
        this->pronunciation = this->lyric;          // 1. lyric fallback
    if (this->candidates.empty() && !this->pronunciation.empty())
        this->candidates.push_back(this->pronunciation);  // 2. seed candidates
}
```

顺序不能颠倒——若先种子 candidates 再做 lyric fallback，pronunciation 为空时 candidates 留空，后续以 candidates 迭代的调用方会漏项。

---

## G2P ONNX Driver Setup helper (A1)

> A3 追加（2026-07-25）。详细设计见
> [docs/lite-integration/02-synthrt-side-changes.md](file:///d:/projects/synthrt/docs/lite-integration/02-synthrt-side-changes.md) §A1。

### 定位

`srt::g2p::setupG2pOnnxDriver` 是与 `srt::driver::setupOnnxInferenceDriver` 平级的
low-level setup helper，由 synthrt 提供，避免每个宿主（ds-editor-lite / dsinfer-cli /
未来 Python 宿主）重复实现 G2P ONNX driver 适配代码（lite 侧原先 110 行 file-local
adapter 类）。**非新增 facade**：它不包装 `VoicebankSession` 或 `LanguageService`，
仅做一次性的 driver 注册，调用方按需组合。

头文件：[include/synthrt/G2P/G2pOnnxSetup.h](file:///d:/projects/synthrt/include/synthrt/G2P/G2pOnnxSetup.h)

### 行为

复用 Runtime 的推理 ONNX driver（由 `srt::driver::setupOnnxInferenceDriver` 在
`inference` 类别下注册的 `"dsdriver"` 对象），为 G2P Manager 注册一个 CPU-only
SessionFactory adapter：

1. 在进程级 `srt::g2p::Manager` 上调用 `addPluginPath()` 注册 G2P 插件搜索路径
   （task + driver IID）；
2. 从 Runtime 的 `inference` 类别取出 `"dsdriver"`，转型为 `srt::driver::InferenceDriver`；
3. 用一个 CPU-only SessionFactory adapter 包装它，对每次 session `open()` 强制
   `useCpu=true`（G2P 不得与 GPU 推理争用）；
4. 在 Manager 的 `kDriverCategory` 类别下以 `kG2pOnnxDriverName`（`"g2pOnnxDriver"`）
   注册该 adapter。

**幂等**：重复调用安全（plugin path 由 PluginFactory 去重；同名 driver 对象被替换）。
**不自动 fallback**：若 `"dsdriver"` 缺失，返回 `InferenceNotInitialized`，由调用方
决定是否继续。

### 调用顺序

```
srt::driver::setupOnnxInferenceDriver(runtime, ...)   // 1. 先注册推理 ONNX driver
srt::g2p::setupG2pOnnxDriver(runtime, g2pPluginPaths) // 2. 复用 dsdriver 注册 G2P driver
LanguageService::initializeMetadata(...)              // 3. 注册 G2P 包路由
LanguageService::initializeModels()                   // 4. 加载 G2P ONNX 模型
```

> 当 `VoicebankSession(SessionResources{...})` 的 `languageService` 非空时
> （实际触发条件为 `if (svc)` 即 `languageService != nullptr`，非 `g2pPluginPaths` 非空），
> `refresh()` 内部会自动调用 `LanguageService::initializeMetadata/updateMetadata`，
> 传入 `g2pPluginPaths` + `officialG2pPackages` 作为参数
> （见 [ds-session.md](ds-session.md)），调用方无需手动执行第 3 步。

### 最小用法

```cpp
#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Driver/OnnxSetup.h>
#include <synthrt/G2P/G2pOnnxSetup.h>

#include <filesystem>
#include <vector>

srt::core::Runtime runtime;
srt::driver::OnnxDriverConfig config;
// config.ep = srt::driver::onnx::DMLExecutionProvider; ...

// 1. 先注册推理 ONNX driver（在 Runtime 的 "inference" 类别下注册 "dsdriver"）
srt::driver::setupOnnxInferenceDriver(runtime, pluginRoot, config);

// 2. 复用推理 driver 注册 G2P ONNX driver
std::vector<std::filesystem::path> g2pPluginPaths = {
    pluginRoot / "srt-g2p/G2ps",
    pluginRoot / "srt-g2p/dict",
};
auto exp = srt::g2p::setupG2pOnnxDriver(runtime, g2pPluginPaths);
if (!exp) {
    // exp.error() 通常是 InferenceNotInitialized（未先调用 setupOnnxInferenceDriver）
    // 或 SessionError（plugin path / category 注册失败）
}
```

### 验证

参见 [docs/lite-integration/05-verification-checklist.md](file:///d:/projects/synthrt/docs/lite-integration/05-verification-checklist.md) §A1（A1-T01 ~ A1-T06）。

