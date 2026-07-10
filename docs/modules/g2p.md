# G2P 模块 (`srt::g2p` / `ds::lang`)

namespace: `srt::g2p` (G2P 框架) / `ds::lang` (LanguageService) | target: `synthrt::g2p` | 头文件: `include/synthrt/G2P/`

---

## 职责

G2P (Grapheme-to-Phoneme) 模块负责歌词到音素的转换：
- `LanguageService` (`ds::lang`) — 语言服务入口，初始化 G2P Manager、解析语言路由、批量转换
- `Manager` (`srt::g2p`) — G2P 管理器（进程级单例）
- `PackageManager` — G2P 包管理
- G2P 插件 — chain/lstm/mandarin/cantonese/ds-dict

---

## 关键 API

### LanguageService

```cpp
// include/synthrt/G2P/LanguageService.h
namespace ds::lang;

class LanguageService {
public:
    // 初始化（调用一次）
    // packageDirs 来自 VoicebankScanner::packageDirectory()
    Expected<void> initialize(
        const std::vector<std::filesystem::path> &pluginSearchPaths,
        const std::vector<std::filesystem::path> &officialG2pPackagePaths,
        const std::unordered_map<std::string, std::filesystem::path> &packageDirs);

    // 解析语言路由（singer + language → G2P/S2P/Onset 资源）
    Expected<LanguageRoute> resolveLanguageRoute(
        const std::string &packageId,
        const std::string &singerId,
        const std::string &languageId) const;

    // 批量 G2P 转换（不解析路由，直接用 Manager）
    std::vector<srt::g2p::G2pRes> convertLyric(
        const std::vector<srt::g2p::G2pInput> &input) const;

    // 便捷转换（路由解析 + G2P，可选错误输出）
    bool convert(const std::string &packageId,
                 const std::string &singerId,
                 const std::string &languageId,
                 const std::vector<srt::g2p::G2pInput> &inputs,
                 std::vector<srt::g2p::G2pRes> &outputs,
                 srt::core::Diagnostic *error = nullptr) const;
};
```

### LanguageRoute

```cpp
// include/diffsinger/Lang/LanguageRoute.h
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

## 初始化流程（4 阶段）

```cpp
Expected<void> LanguageService::initialize(
    pluginSearchPaths, officialG2pPackagePaths, packageDirs) {
    auto mgr = srt::g2p::Manager::instance();
    const bool alreadyInitialized = mgr->initialized();

    // Stage 1: 注册 G2P 插件搜索路径（仅首次）
    if (!alreadyInitialized) {
        for (const auto &path : pluginSearchPaths) { /* ... */ }
    }

    // Stage 2: 注册官方 G2P 包（仅首次，默认上下文失败阻塞）
    if (!alreadyInitialized) {
        for (const auto &path : officialG2pPackagePaths) { /* ... */ }
    }

    // Stage 3: 注册 voicebank 私有 G2P 包（始终执行，依赖当前实例 packageDirs）
    for (const auto &kv : packageDirs) {
        // parsePackage → resolveG2pRoute → addPackagePath
        // 失败记录 Diagnostic 但不中断（非默认上下文容错）
    }

    // Stage 4: 初始化 Manager（仅首次）
    if (!alreadyInitialized) {
        auto g2pResult = mgr->initialize();
        if (!g2pResult) return g2pResult.takeError();
    }
    return {};
}
```

**关键约束**:
- G2P Manager 是进程级单例，多 LanguageService 实例共享
- Stage 3 始终执行（ER-08 修复），确保多会话场景下 voicebank G2P 正确注册
- ONNX 驱动 (`g2pOnnxDriver`) 必须在 `initialize()` 之前注册

---

## G2P 插件

| 插件 | 目录 | 说明 |
|---|---|---|
| chain | `plugins/G2P/chain/` | 链式 G2P（多步骤管线：dict → model → format → validate） |
| lstm | `plugins/G2P/lstm/` | LSTM G2P（ONNX 模型） |
| mandarin | `plugins/G2P/mandarin/` | 普通话拼音 G2P |
| cantonese | `plugins/G2P/cantonese/` | 粤语 G2P |
| ds-dict | `plugins/G2P/ds-dict/` | DiffSinger 字典 G2P |

### Chain G2P 管线

`plugins/G2P/chain/internal/Core/G2pPipeline.cpp` 实现多步骤管线：

```
DictStep → ModelStep → FormatStep → TagAndValidateStep → FallbackStep
```

每个步骤实现 `G2pStep::process()`，失败时由 `FallbackStep` 兜底。

---

## 调用关系

```
宿主层
  ├── langSvc.initialize(g2pPluginPaths, officialG2pPaths, packageDirs)
  │     └── Manager::instance()->addPackagePath() / initialize()
  │
  ├── langSvc.resolveLanguageRoute(packageId, singerId, languageId)
  │     └── 返回 LanguageRoute (g2pId + s2pFile + onsetFile)
  │
  └── langSvc.convert(packageId, singerId, languageId, inputs, outputs)
        ├── resolveLanguageRoute()
        └── convertLyric(inputs)
              └── Manager::instance()->convert() → G2P 插件执行
```

---

## 与其他模块的协作

- **S2P**: `LanguageRoute.s2pFile` 指向 S2P 资源，由宿主层调用 S2P 模块
- **ds-bank**: `packageDirs` 来自 `VoicebankScanner::packageDirectory()`
- **ONNX Driver**: lstm 插件依赖 ONNX 驱动，必须在 `initialize()` 前注册

---

## G2P 错误系统

`srt::g2p::Error` 继承 `srt::core::Error`，v4 起使用 `srt::core::ErrorCode`（G2p* 代码段 300-399）替代原有的 12 值 Type 枚举。旧 Type 枚举标记为 `[[deprecated]]` 但仍可用。

```cpp
// include/synthrt/G2P/Support/Error.h
class Error : public srt::core::Error {
public:
    using srt::core::ErrorCode;  // 引入 G2p* 代码

    // 新构造函数（ErrorCode + 自动源位置）
    Error(ErrorCode code, std::string msg,
          const std::source_location &loc = std::source_location::current());
    Error(ErrorCode code, std::string msg, std::string suggestion,
          const std::source_location &loc = std::source_location::current());

    // ok() 覆写：检查 ErrorCode::G2pSuccess
    bool ok() const noexcept override;

    [[deprecated]] enum Type { Success=0, ConfigError=1, ..., AlreadyInitialized=11 };
};
```

### G2P 错误码

| ErrorCode | 说明 |
|---|---|
| `G2pSuccess` (300) | 成功 |
| `G2pConfigError` | 配置错误 |
| `G2pFileSystemError` | 文件系统错误 |
| `G2pDependencyError` | 依赖错误 |
| `G2pRuntimeError` | 运行时错误 |
| `G2pRouteNotFound` | 语言路由未找到 |
| `G2pPackageNotFound` | G2P 包未找到 |
| `G2pConversionFailed` | 转换失败 |
| `G2pAlreadyInitialized` | Manager 已初始化 |
| ... | 完整列表见 `Diagnostic.h` |

错误创建使用 `Error::g2pError()` 工厂函数，自动填充 language/packageId 上下文和源位置。

### resolveLanguageRoute 错误码

| 场景 | ErrorCode |
|---|---|
| packageId 不在 packageDirs 中 | `G2pPackageNotFound` |
| singerId 在包中未找到 | `G2pRouteNotFound` |
| 语言未配置 G2P | `G2pRouteNotFound` |
