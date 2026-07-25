# 本项目（synthrt）侧改动

日期: 2026-07-26（v7 修订：auto-init 触发条件勘误 + 测试文件声明勘误 + CMake 描述勘误；v6 修订：补全 A1/A2/A3 完成状态；v3 修订：核对实际代码）
状态: ☑ Phase A 全部完成（A1=37e5b3d, A2=d0015af, A3=4e0a3ff；**v7 勘误：A1/A2 单元测试文件未创建，待 C1 阶段补齐**；C1/C2 待回归）
原则: 严格遵循 user_rules "尽量少的谨慎改动"；ARCH-02 Level=2 仅追加不修改签名；ARCH-03 不新增 facade；以 lite 为主体大胆追加 lite 真正需要的便捷 API。

---

> **v6 状态同步**：synthrt 侧改动（A1 + A2 + A3）已全部落地。
> - A1 `setupG2pOnnxDriver` helper 已新增（commit 37e5b3d） — lite 110 行 adapter 代码已删除并迁移至 A1
> - A2 `VoicebankSnapshot::findSinger/findSingersBySingerId/findPackage/findManifest` 已新增（commit d0015af） — `VoicebankSnapshot` 现已含 4 个 const 查询方法
> - A3 文档更新已完成（commit 4e0a3ff） — `docs/modules/{overview,g2p,ds-session}.md` 均已更新
> - **关键发现**：`SessionResources.languageService` 非空时，`VoicebankSession::refresh()` 内部自动调用 `LanguageService::initializeMetadata()`（首次）/`updateMetadata()`（增量），将 `g2pPluginPaths` + `officialG2pPackages` 作为参数传入（[VoicebankSession.cpp#L583-L620](file:///d:/projects/synthrt/domains/ds-session/lib/VoicebankSession.cpp) — 实际触发条件为 `if (svc)` 即 `languageService != nullptr`，非 `g2pPluginPaths` 非空）。因此 lite **无需手动初始化 LanguageService**，v2 的 B2 阶段已合并到 B1a（见 [03-lite-side-migration.md](file:///d:/projects/synthrt/docs/lite-integration/03-lite-side-migration.md)）

---

## 总览

经 [01-current-state-analysis.md](file:///d:/projects/synthrt/docs/lite-integration/01-current-state-analysis.md) §3 验证，synthrt 现有 API 已覆盖 lite 全部 voicebank/G2P/inference 需求。**以 lite 为主体**审视后识别 2 处可追加的便捷 API，使 lite 能彻底删除中间映射层：

| ID | 类型 | 内容 | LOC 估算 |
|---|---|---|---|
| A1 | 新增 API | `srt::g2p::setupG2pOnnxDriver(runtime, g2pPluginPaths)` helper | +80 (新文件) |
| A2 | 追加方法 | `VoicebankSnapshot::findSinger/findPackage/findManifest` const 查询方法 | +30 (修改 header) |
| A3 | 文档 | `docs/modules/overview.md` / `g2p.md` 更新 | +15 行 |

不修改任何现有 API 签名、不删除 deprecated 接口、不动公共头文件 `_` 前缀成员（CS-03 冻结）。

---

## A1: 追加 `srt::g2p::setupG2pOnnxDriver` helper — ☑ 已完成（commit 37e5b3d）

### 动机

> **v6 历史快照**：lite B1c（commit f652052f, 2026-07-25）已删除此处描述的 adapter 类与 `initializeG2pOnnxDriver()` 方法。本节保留作为 A1 设计动机的历史记录，引用行号在 B1c 删除后已失效，请勿用于查找代码。

B1c 前的 [SynthrtEngine.cpp](file:///d:/projects/ds-editor-lite/src/app/Modules/SynthrtEngine/SynthrtEngine.cpp)（原 L73-L184，已删除）曾定义两个 file-local adapter 类（`G2pOnnxSessionTask` / `G2pOnnxSessionFactory`）+ `initializeG2pOnnxDriver()` 方法，共约 110 行通用样板。该代码：

1. 把 G2P `srt::g2p::SessionTask` 适配到推理 `srt::driver::InferenceSession`，强制 `useCpu=true`（避免 G2P 与推理争用 GPU）
2. 持有 `shared_ptr<InferenceDriver>` 防 Runtime ObjectPool 销毁先于 G2P Manager
3. 注册到 `srt::g2p::Manager` 的 `kDriverCategory` 类别，名为 `kG2pOnnxDriverName`

属于跨模块边界适配代码（G2P 框架 ↔ Driver 框架），与既有的 [srt::driver::setupOnnxInferenceDriver](file:///d:/projects/synthrt/include/synthrt/Driver/OnnxSetup.h) 平级，应由 synthrt 提供，避免每个宿主（lite / dsinfer-cli / 未来 Python 宿主）重复实现。

### API 设计

新文件 `include/synthrt/G2P/G2pOnnxSetup.h`：

```cpp
#pragma once

#include <filesystem>
#include <vector>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/srt_g2p_global.h>

namespace srt::core {
    class Runtime;
}

namespace srt::g2p {

    /// Setup the G2P ONNX driver by reusing the Runtime's inference ONNX driver
    /// (registered as "dsdriver" in the "inference" category by
    /// srt::driver::setupOnnxInferenceDriver).
    ///
    /// This is the G2P-side counterpart of setupOnnxInferenceDriver. It:
    ///   1. Registers G2P plugin search paths (task + driver IIDs) on the
    ///      process-level srt::g2p::Manager via addPluginPath().
    ///   2. Locates the inference "dsdriver" object in the Runtime's "inference"
    ///      category and casts it to srt::driver::InferenceDriver.
    ///   3. Wraps it with a CPU-only SessionFactory adapter that forces
    ///      useCpu=true on every session open() (G2P must not compete with
    ///      GPU inference).
    ///   4. Registers the adapter in the Manager's kDriverCategory under the
    ///      name kG2pOnnxDriverName ("g2pOnnxDriver").
    ///
    /// Prerequisite: srt::driver::setupOnnxInferenceDriver() must have been
    /// called on the same Runtime first; otherwise the "dsdriver" object is
    /// missing and this function returns InferenceNotInitialized.
    ///
    /// \param runtime         The Runtime whose "inference/dsdriver" object is reused.
    /// \param g2pPluginPaths  G2P plugin search directories (typically
    ///                         <pluginRoot>/srt-g2p/G2ps and <pluginRoot>/srt-g2p/dict).
    /// \return Expected<void> — InferenceNotInitialized if "dsdriver" missing,
    ///                         SessionError on plugin path / category failures.
    srt::core::Expected<void> SRT_G2P_EXPORT setupG2pOnnxDriver(
        srt::core::Runtime &runtime,
        const std::vector<std::filesystem::path> &g2pPluginPaths);

} // namespace srt::g2p
```

### 实现要点（新文件 `lib/G2P/G2pOnnxSetup.cpp`）

1. **搬迁** lite 现有 `G2pOnnxSessionTask` / `G2pOnnxSessionFactory` 到 `lib/G2P/G2pOnnxSetup.cpp` 的 anonymous namespace 内。代码逻辑不变，仅做以下规范化：
   - 文件路径用 `stdc::path::from_utf8` 而非字面量（CODING-03）
   - 错误用 `Error(ErrorCode::InferenceNotInitialized, ...)` 显式构造（project_memory: "Error factory functions must be grouped by module"）
   - 异常边界 try-catch `std::exception`（CODING-02）
2. **不自动 fallback**：若 `dsdriver` 缺失，返回 `InferenceNotInitialized` 错误，由调用方决定是否继续（project_memory: "setupOnnxInferenceDriver must NOT auto-fallback to CPU"）。
3. **幂等**：重复调用安全（重新注册 plugin path 由 PluginFactory 去重；`kG2pOnnxDriverName` 对象在 Manager category 中被替换）。
4. **CMake**（**v7 勘误**）：`lib/G2P/CMakeLists.txt` 使用 `file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp")` 自动发现源文件，**无需显式添加 `G2pOnnxSetup.cpp`**（自动被 GLOB 收集）；同时新增 `srt::driver` 到 `LINKS` 列表（原 doc 误称"无需新依赖"，实际 A1 引入了对 `srt::driver::InferenceDriver` 的依赖，注释说明 "srt-driver only depends on srt::core, so no circular dependency is introduced"），并补加 A1 说明注释。

### 调用方迁移

> **v6 历史快照**：lite B1c（commit f652052f, 2026-07-25）已删除 `SynthrtEngine::initializeG2pOnnxDriver()` 方法本体，相关调用已直接合并到 `SynthrtEngine::initialize()` 内部。本节保留作为 A1 调用方迁移意图的历史记录，引用行号在 B1c 删除后已失效，请勿用于查找代码。

迁移到 A1 后，lite `SynthrtEngine::initializeG2pOnnxDriver()` 方法体（原 [SynthrtEngine.cpp#L476-L512](file:///d:/projects/ds-editor-lite/src/app/Modules/SynthrtEngine/SynthrtEngine.cpp)，已删除）由 36 行收敛为：

```cpp
bool SynthrtEngine::initializeG2pOnnxDriver() {
    const std::vector<fs::path> g2pPluginPaths = {
        pluginRoot() / "srt-g2p/G2ps",
        pluginRoot() / "srt-g2p/dict",
    };
    if (auto exp = srt::g2p::setupG2pOnnxDriver(m_runtime, g2pPluginPaths); !exp) {
        qWarning() << "SynthrtEngine: G2P ONNX driver not available:"
                   << QString::fromUtf8(exp.error().message());
        return false;
    }
    return true;
}
```

（lite 侧的 `G2pOnnxSessionTask` / `G2pOnnxSessionFactory` 两个 file-local 类已在 B1c 完全删除）

### 验证项

> **v7 勘误**：原计划新增 `unittests/G2P/tst_g2p_onnx_setup.cpp`，但 commit 37e5b3d 实际未包含该测试文件（CMakeLists.txt 也未引用）。C1 阶段需补齐。

- **待新增**单元测试 `unittests/G2P/tst_g2p_onnx_setup.cpp`（**v7 勘误：当前未创建**）：
  - 无 dsdriver → 返回 `InferenceNotInitialized`
  - 有 dsdriver → 成功注册 `kG2pOnnxDriverName`
  - 重复调用幂等
  - 异常边界（构造 InferenceDriver 抛异常 → 返回 `GenericError`）
- 回归 `unittests/G2P/tst_g2p_*.cpp` 现有 case（不依赖 setupG2pOnnxDriver 的部分）

---

## A2: VoicebankSnapshot 追加 const 查询方法 — ☑ 已完成（commit d0015af）

### 动机

lite 在多处场景需按 `SingerRef` 反查 singer / 按 `(packageId, version)` 反查 package 与 manifest：

| lite 调用点 | 当前实现 | 频率 |
|---|---|---|
| `InferEngine::acquireSingerSession` | 遍历 catalog packages | 每次 task 启动 |
| `PackageManager::findPackageByIdentifier` | 遍历 `m_packageLocator` | UI 每次刷新 |
| `PackageManager::findSingerByIdentifier` | 遍历 `m_singerLocator` | UI 每次查询 |
| `SynthrtEngine::singerSnapshot` | `findSinger` 遍历 | task 启动 |
| `SynthrtEngine::packageDirectory` | `findPackage` 遍历 | Runtime::loadPackage 前 |

每次都是 O(n) 遍历，n 通常 <100。性能可接受，但**代码重复**：5 个调用点各自写遍历逻辑。VoicebankSession 内部已有私有 `findSinger(snapshot, key)` 但不暴露。

**以 lite 为主体**的解决：在 `VoicebankSnapshot` 追加 const 查询方法，让 lite 直接调用，消除重复遍历代码。这比让 lite 自己写遍历更符合 ARCH-04"直接句柄"。

### API 设计

修改 `include/diffsinger/Session/VoicebankSession.h`，在 `struct VoicebankSnapshot` 内追加 3 个 const 方法：

```cpp
struct DSSESSION_EXPORT VoicebankSnapshot {
    // ... 现有字段不变 ...

    /// Find a singer snapshot by exact (packageId, singerId, version) match.
    /// Returns nullptr if not found. Version comparison uses
    /// stdc::VersionNumber::fromString() normalization (empty version matches
    /// only empty; "1.0" matches "1.0.0" after normalization).
    const ds::bank::SingerSnapshot *findSinger(const ds::bank::SingerRef &ref) const;

    /// Find singers by singerId alone (may return multiple for multi-version
    /// same-packageId scenarios). Returned vector is non-owning; valid only
    /// while the snapshot is alive.
    std::vector<const ds::bank::SingerSnapshot *>
        findSingersBySingerId(const std::string &singerId) const;

    /// Find a package status by (packageId, version) match. Returns nullptr
    /// if not found (including invalid packages — they appear in `packages`
    /// with valid=false, so this returns them too).
    const ds::bank::PackageStatus *
        findPackage(const std::string &packageId,
                    const stdc::VersionNumber &version) const;

    /// Find a package manifest by (packageId, version) match. Returns
    /// nullptr if not found or the package is invalid (invalid packages
    /// have no manifest entry — TD-01).
    const ds::bank::PackageManifest *
        findManifest(const std::string &packageId,
                     const stdc::VersionNumber &version) const;
};
```

### 实现要点

实现写在 `domains/ds-session/lib/VoicebankSession.cpp` 内（与现有 snapshot 构建逻辑同文件）：

- 4 个方法都是 const + 线性遍历，**O(n) 复杂度**
- `findSinger(SingerRef)` 内部用 `SingerRef::operator==` 比较（已定义）
- `findPackage` / `findManifest` 用 `(packageId, version)` 比较，version 比较通过 `stdc::VersionNumber::operator==`（已处理 "1.0" == "1.0.0" 规范化，project_memory 已要求）
- 不引入索引 map（snapshot 是不可变的，调用方频繁时可在调用方层加缓存）

### 与 A1 的关系

A2 不依赖 A1，可并行实现；但二者都属"以 lite 为主体追加便捷 API"，建议同一 Phase A 一起合入。

### 验证项

> **v7 勘误**：原计划新增 `domains/ds-session/unittests/tst_vbs_snapshot_query.cpp`，但 commit d0015af 实际未包含该测试文件（CMakeLists.txt 也未引用）。C1 阶段需补齐。

- **待新增**单元测试 `domains/ds-session/unittests/tst_vbs_snapshot_query.cpp`（**v7 勘误：当前未创建**）：
  - `findSinger(SingerRef{pkg, singer, "1.0.0"})` → 命中
  - `findSinger(SingerRef{pkg, singer, "1.0"})` → 命中（版本规范化）
  - `findSinger(SingerRef{pkg, singer, "2.0.0"})` → nullptr
  - `findSingersBySingerId("singerA")` 多版本场景返回 2 个
  - `findPackage(pkg, ver)` → 命中 valid package
  - `findPackage(pkg, ver)` → 命中 invalid package（仍返回，调用方查 `valid` 字段）
  - `findManifest(pkg, ver)` → valid package 命中；invalid package 返回 nullptr
- 回归 `domains/ds-session/unittests/tst_vbs_snapshot.cpp` / `tst_voicebank_session_snapshot_ensure.cpp`

---

## A3: 文档更新 — ☑ 已完成（commit 4e0a3ff）

### `docs/modules/overview.md` §3 顶部插入 lite 推荐入口提示

在 §3.1 "会话式调用（vnext 推荐，宿主层唯一入口）" 标题下方追加：

```markdown
> **lite 对接入口**：ds-editor-lite 应使用 `VoicebankSession(SessionResources{runtime, languageService})`
> 作为唯一 voicebank/G2P/inference 入口，避免在 SynthrtEngine 中重复实现目录包装、
> 句柄映射与刷新阻塞逻辑。详细迁移方案见 [docs/lite-integration/](../lite-integration/)。
```

### `docs/modules/g2p.md` 在 `LanguageService` 节末尾追加 setup helper 交叉引用

```markdown
### G2P ONNX Driver Setup

`srt::g2p::setupG2pOnnxDriver(runtime, g2pPluginPaths)` 复用 Runtime 的推理
ONNX driver（由 `srt::driver::setupOnnxInferenceDriver` 注册的 "dsdriver"），
注册 G2P 插件搜索路径并创建 CPU-only SessionFactory adapter。
调用顺序：`setupOnnxInferenceDriver` → `setupG2pOnnxDriver` →
`LanguageService::initializeMetadata` → `initializeModels`。
```

### `docs/modules/ds-session.md` 在 `VoicebankSnapshot` 节追加查询方法说明

```markdown
### Snapshot 查询便捷方法

`VoicebankSnapshot` 提供 4 个 const 查询方法供宿主快速定位 singer/package/manifest，
避免在调用方重复 O(n) 遍历：

- `findSinger(SingerRef)` — 精确匹配（packageId, singerId, version）
- `findSingersBySingerId(singerId)` — 多版本场景返回多个
- `findPackage(packageId, version)` — 包括 invalid 包
- `findManifest(packageId, version)` — 仅 valid 包有 manifest
```

---

## 不在 synthrt 侧范围的项

以下项**不在本轮改动**，列出避免误改：

| 项 | 原因 |
|---|---|
| 修改 `VoicebankSession` 公共 API（除 A2 追加方法外） | ARCH-02 Level=2 冻结；现有 API 已覆盖 lite 需求 |
| 删除 `LanguageService::initialize(map)` 等 deprecated 接口 | INFRA-02 规划 Level=3 清理，本轮仅迁移调用方 |
| 新增 "SynthrtHostSession" 包装 `VoicebankSession` | 违反 ARCH-03 "synthrt 不新增 facade 或转发层" |
| `srt_session_create_with_resources` 等价 C++ helper | lite 走 C++ 接口，不通过 C ABI |
| 修改 `ModelSetHandle` 接口 | 现有 `load/start/stop/reset/unload/unloadAll/isLoaded/isStale/stages` 已完整覆盖 ARCH-05 |
| 把 extractors 集成到 `VoicebankSession` | extractors 是 lite 编辑器特定能力（rmvpe/game），与 voicebook/G2P/inference 无关 |
| 修改 `srt::driver::setupOnnxInferenceDriver` 增加 G2P 适配 | 违反"组合优于转发"，G2P 适配应独立 setup（A1），由调用方按需组合 |
| 在 `VoicebankSession` 暴露 `findSinger` 公共方法（替代 A2） | `findSinger` 已是 `VoicebankSnapshot` 的查询职责，不应放在会话层 |

---

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| A1 新 API 与 lite 既有 adapter 行为不一致（如 useCpu 强制点遗漏） | 实现直接搬迁 lite 现有代码，保留同一作者审查；增加对比测试 |
| `Manager` 是进程级单例，setupG2pOnnxDriver 全局副作用 | 与 lite 现状一致；setup helper 仅在显式调用时注册，不做隐式初始化 |
| A2 查询方法语义与 VoicebankSession 内部 `findSinger` 不一致 | A2 是 snapshot 自身方法，不依赖 session；VersionNumber 规范化由 stdc 保证 |
| 未来 Python 宿主可能不需要 G2P | setupG2pOnnxDriver 是可选 helper，调用方按需调用 |

---

## 提交策略 — ☑ 已全部完成

按 user_rules "完成单个任务后单独提交但不推送"：

- ☑ Commit A1 (37e5b3d, 2026-07-25): 新增 `include/synthrt/G2P/G2pOnnxSetup.h` + `lib/G2P/G2pOnnxSetup.cpp` + CMakeLists.txt 注释更新（**v7 勘误：未含单元测试，待 C1 补齐**）
- ☑ Commit A2 (d0015af, 2026-07-25): 修改 `include/diffsinger/Session/VoicebankSession.h` + `domains/ds-session/lib/VoicebankSession.cpp`（**v7 勘误：未含单元测试，待 C1 补齐**）
- ☑ Commit A3 (4e0a3ff, 2026-07-25): 文档更新 `docs/modules/overview.md` + `g2p.md` + `ds-session.md`

三个 commit 独立、不推送，按 user_rules 在 `docs/lite-integration/00-overview.md` §4 执行顺序表中标记完成状态。
