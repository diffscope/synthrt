# S2P 模块 (`srt::s2p`)

namespace: `srt::s2p` | target: `synthrt::s2p` | 头文件: `include/synthrt/S2P/`

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

S2P 不被 LanguageService 直接调用，而是由宿主层根据 `LanguageRoute` 选择调用：

```
宿主层
  ├── langSvc.resolveLanguageRoute(packageId, singerId, languageId)
  │     └── 返回 LanguageRoute { s2pMode, s2pFile, onsetFile }
  │
  ├── 根据 s2pMode 创建 S2P 实例
  │     ├── "dict" → DictionaryS2P(s2pFile)
  │     └── "direct" → DirectS2P(s2pFile)
  │
  ├── s2p.convert(phonemes) → 发音符号
  │
  └── 根据 onsetFile 创建 OnsetMarker
        ├── RuleOnsetMarker(onsetFile)
        └── LuaOnsetMarker(onsetFile)
```

---

## Lua 执行环境

`lib/S2P/internal/LuaExecutionEnvironment.h` 封装 LuaJIT 执行环境，用于 `LuaS2P` 和 `LuaOnsetMarker`。LuaJIT 依赖通过 vcpkg 管理。

---

## 与其他模块的协作

- **G2P**: `LanguageService` 解析路由后返回 S2P 资源路径，宿主层调用 S2P
- **Core**: 依赖 `srt::core` 基础设施

---

## 错误处理

S2P 错误使用 `ErrorCode::S2p*` 代码段（500-599）：`S2pResourceNotFound`、`S2pConversionFailed`、`S2pScriptError`、`S2pDictionaryError`。
