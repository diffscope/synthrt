# S2P 模块 (`srt::s2p`)

namespace: `srt::s2p` | target: `srt::s2p` | 头文件: `include/synthrt/S2P/`

---

## 职责

S2P (Symbol-to-Phoneme) 模块负责音素符号到发音符号的转换，以及 onset 检测：
- `DictionaryS2P` — 基于字典的 S2P
- `DirectS2P` — 直接映射 S2P
- `MappingS2P` — 映射表 S2P
- `LuaS2P` — Lua 脚本 S2P
- `RuleOnsetMarker` / `LuaOnsetMarker` — onset 检测

---

## 关键 API

S2P 模块提供多种 S2P 策略，由 `LanguageRoute.s2pMode` 决定使用哪种：

| s2pMode | 实现 | 说明 |
|---|---|---|
| `"dict"` | `DictionaryS2P` | 基于字典查询 |
| `"direct"` | `DirectS2P` | 直接映射 |

`LuaS2P` 和 `MappingS2P` 由具体配置决定使用时机。

---

## 调用关系

S2P 资源可通过两种方式获取，均最终调用 `LanguageResource::convert()`：

### 方式一：跟随 G2P 路由（耦合）

`resolveLanguageRoute()` 同时返回 G2P 路由和 S2P 资源路径，适合 G2P+S2P 一次完成的场景：

```
宿主层
  ├── langSvc.resolveLanguageRoute(packageId, singerId, languageId)
  │     └── 返回 LanguageRoute { g2pId, s2pMode, s2pFile, onsetFile, ... }
  │
  ├── 根据 s2pMode/onsetFile 手动构造 LanguageResource
  │     ├── "dict" → LanguageResource::dictionary(s2pFile, onsetFile)
  │     └── 其他   → LanguageResource::direct(onsetFile)
  │
  └── resource.convert(pronunciation) → SyllablePronunciation { phonemes[], onsets[] }
```

### 方式二：独立解析（推荐）

`resolveS2pResource()` 单独解析 S2P 资源并缓存，适合用户编辑发音后仅需重跑 S2P 的场景：

```
宿主层
  ├── langSvc.resolveS2pResource(packageId, singerId, languageId)
  │     ├── 内部调用 resolveLanguageRoute() 获取路由
  │     ├── 根据 s2pMode/onsetFile 构造 LanguageResource
  │     ├── 缓存 shared_ptr<LanguageResource>（key: packageId/singerId/languageId）
  │     └── 返回 Expected<shared_ptr<LanguageResource>>
  │
  └── resource->convert(pronunciation) → SyllablePronunciation { phonemes[], onsets[] }
```

缓存使用 `shared_mutex` 实现读多写少的线程安全访问，同一 (packageId, singerId, languageId) 组合只构造一次。

---

## Lua 执行环境

`lib/S2P/internal/LuaExecutionEnvironment.h` 封装 LuaJIT 执行环境，用于 `LuaS2P` 和 `LuaOnsetMarker`。LuaJIT 依赖通过 vcpkg 管理。

---

## 与其他模块的协作

- **G2P**: `LanguageService` 提供 `resolveS2pResource()` 直接返回 `shared_ptr<LanguageResource>`；也可通过 `resolveLanguageRoute()` 获取 S2P 路径自行构造。srt-g2p 公开链接 srt::s2p
- **Core**: 依赖 `srt::core` 基础设施

---

## 错误处理

S2P 错误使用 `ErrorCode::S2p*` 代码段（500-599）：`S2pResourceNotFound`、`S2pConversionFailed`、`S2pScriptError`、`S2pDictionaryError`。
