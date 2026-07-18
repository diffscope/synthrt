# G2P 模块 (`srt::g2p` / `ds::lang`)

namespace: `srt::g2p` (G2P 框架) / `ds::lang` (LanguageService) | target: `synthrt::g2p` | 头文件: `include/synthrt/G2P/`

---

## 职责

G2P (Grapheme-to-Phoneme) 模块负责歌词到音素的转换：
- `LanguageService` (`ds::lang`) — 语言服务入口，初始化 G2P Manager、解析语言路由、批量转换、S2P 资源解析
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
namespace ds::lang;

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

### updateMetadata 热重载 (V3-16 / WP8)

`updateMetadata(newPluginPaths, newOfficialPaths, newPackageDirs)` 计算 diff：

- `added` → `addPackagePath(context, version, path)` 注册新 context
- `removed` → `PackageManager::removeContextsByPrefix(prefix, version)` **版本感知**移除（D-43）
- `unchanged` → 不重新注册

`VoicebankSession::refresh()` 在扫描完成后调用 `updateMetadata()`，把退役 voicebank 的 G2P context 同步下线。若 `metadataReady()==false`，自动降级为完整 `initializeMetadata()` 调用（WP8-session 兜底）。

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
