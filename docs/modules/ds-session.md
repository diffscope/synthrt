# DS Session 模块 (`ds::session`)

namespace: `ds::session` | target: `srt-ds::session` | 头文件: `include/diffsinger/Session/`

---

## 职责

DS Session 模块是宿主层(ds-editor-lite)使用声库和 G2P 的唯一入口。它封装了：
- 声库扫描与原子快照发布(`VoicebankSession::refreshAsync`)
- 不可变快照查询(`VoicebankSession::snapshot`)
- 能力摘要(`VoicebankSession::capabilitySummary`)
- G2P/S2P 转换(`VoicebankSession::convertG2p` / `convertS2p`)
- 最终音素校验(`VoicebankSession::validatePhonemes`)
- 模型集生命周期(`VoicebankSession::createModelSet` / `ModelSetHandle`)

设计目标(vnext)：
- session 任意时刻只暴露一份完整快照
- 公开摘要仅为 `Ready`/`Warning`/`Disabled` 与结构化诊断
- C++ 使用同步 `Expected`；Lite 决定异步调度
- 严格包隔离、依赖解算、资源锁定、安全排空均封装在 session 内
- Lite 缓存使用完整运行指纹，不能跨 snapshot/model/provider/device 复用结果

---

## 依赖关系

- 依赖 `srt-ds::bank`(`VoicebankScanner`、`SingerRef`、`SingerSnapshot`、`PackageStatus`)
- 依赖 `srt::g2p`(`LanguageService`，注入式使用)
- 依赖 `srt-ds::infer`(`ModelSet`、`SingerStageResolver`、`StageSet`、`StageKind`)
- 依赖 `srt::core`(`Runtime`、`Expected`、`Error`、`Diagnostic`、`ErrorCode`)
- 依赖 `srt::s2p`(`LanguageResource::convert`，经 `LanguageService` 间接调用)
- 依赖 `stdcorelib`(`VersionNumber`，用于 `PackageCoordinate.version`)

---

## 关键 API

### VoicebankSession

```cpp
// include/diffsinger/Session/VoicebankSession.h
namespace ds::session;

class DSSESSION_EXPORT VoicebankSession {
public:
    VoicebankSession();
    ~VoicebankSession();
    VoicebankSession(const VoicebankSession &) = delete;
    VoicebankSession &operator=(const VoicebankSession &) = delete;

    // —— 配置：调用方可随时更新，需在下一次 refreshAsync 才生效 ——
    void setRoots(std::vector<std::filesystem::path> roots);
    std::vector<std::filesystem::path> roots() const;
    void setReservedPhonemes(std::vector<std::string> phonemes);
    std::vector<std::string> reservedPhonemes() const;

    // 注入 G2P/S2P 使用的 LanguageService。session 不负责其初始化生命周期；
    // 调用方必须先 initialize() 它。传 nullptr 会禁用 G2P/S2P
    // (后续 convert* 返回 ErrorCode::G2pNotImplementedError)。
    void setLanguageService(std::shared_ptr<srt::g2p::LanguageService> service);
    std::shared_ptr<srt::g2p::LanguageService> languageService() const;

    // 注入 createModelSet 使用的 Runtime。session 不负责其生命周期；
    // 调用方必须先 scanPackages()/loadPackage() 加载歌手包。传 nullptr 会
    // 禁用 ModelSet 创建(后续 createModelSet 返回 InferenceNotInitialized)。
    void setRuntime(srt::core::Runtime *runtime);
    srt::core::Runtime *runtime() const;

    // —— 刷新：返回 shared_future，同 session 并发调用合并为一个 task ——
    std::shared_future<RefreshResult> refreshAsync();

    // —— 快照查询：返回当前快照的共享不可变指针，可能为空 ——
    std::shared_ptr<const VoicebankSnapshot> snapshot() const;
    AvailabilitySummary availability() const;

    // 按歌手 ref 取 Lite-facing 能力摘要。未找到或无 capabilityReport
    // 时返回 Disabled(携带 SvsSingerNotFound 诊断)。
    SingerCapabilitySummary capabilitySummary(const ds::bank::SingerRef &singerKey) const;

    // G2P 转换：经注入的 LanguageService 路由。不复制 fallback、不写空音素。
    srt::core::Expected<std::vector<srt::g2p::G2pRes>>
        convertG2p(const ds::bank::SingerRef &singerKey,
                   const std::string &language,
                   const std::vector<srt::g2p::G2pInput> &inputs) const;

    // S2P 转换：解析歌手级 S2P 资源后调用 LanguageResource::convert()。
    srt::core::Expected<S2pResult>
        convertS2p(const ds::bank::SingerRef &singerKey,
                   const std::string &language,
                   const std::string &pronunciation) const;

    // 最终音素校验：对照歌手 effectivePhonemes + reservedPhonemes。
    // 失败返回 Expected 错误，必须阻断下游推理(不静默替换)。
    srt::core::Expected<void>
        validatePhonemes(const ds::bank::SingerRef &singerKey,
                         const std::vector<std::string> &phonemes) const;

    // 创建绑定当前 snapshot generation 的 ModelSetHandle。singerKey 必须
    // 存在于当前快照且为 Resolved。Runtime 必须已注入并加载歌手包。
    // 刷新成功后旧 handle 的 start() 返回 StaleModelSet。
    srt::core::Expected<std::shared_ptr<ModelSetHandle>>
        createModelSet(const ds::bank::SingerRef &singerKey);
};
```

**线程安全性**：所有公开方法均可并发调用。内部使用 `std::mutex` 保护 `_impl` 状态(roots / reservedPhonemes / languageService / runtime / current snapshot / generation / inFlight)。`snapshot()`、`capabilitySummary()`、`convert*`、`validatePhonemes` 是只读路径，会获取快照共享指针后释放锁；`createModelSet` 在读 Runtime 时短暂持锁，stage 解析与 ModelSet 构造在锁外完成。

### 方法语义与错误路径

| 方法 | 语义 | 成功返回 | 失败路径(ErrorCode) |
|---|---|---|---|
| `setRoots` / `setReservedPhonemes` / `setLanguageService` / `setRuntime` | 更新待用配置，原子地写入内部状态 | — | 不报错 |
| `roots` / `reservedPhonemes` / `languageService` / `runtime` | 读取当前配置(拷贝或共享指针) | 当前值 | 不报错 |
| `refreshAsync` | 提交一次刷新，返回 shared_future。若已有未完成的刷新则返回同一个 future(合并) | `RefreshResult{succeeded=true, ...}` | `RefreshResult{succeeded=false, snapshot=旧快照, errorMessage, diagnostics}` |
| `snapshot` | 返回当前快照共享指针，未刷新时为空 `shared_ptr` | 当前快照或空 | 不报错 |
| `availability` | 聚合快照中所有歌手的可用性计数 | `AvailabilitySummary` | 不报错(无快照时全零) |
| `capabilitySummary` | 按 singerKey 查找并映射到三态 Availability + 列表 | `SingerCapabilitySummary` | 不抛错，错误以 `Diagnostic` 形式塞入 `summary.diagnostics`(`SvsSingerNotFound`) |
| `convertG2p` | 检查 snapshot + 歌手存在 + LanguageService 后委托 `LanguageService::convert` | `Expected<vector<G2pRes>>` | `SessionError`(无快照)、`SvsSingerNotFound`、`G2pNotImplementedError`(无 LanguageService) |
| `convertS2p` | 检查 snapshot + 歌手 + LanguageService 后 `resolveS2pResource` + `convert` | `Expected<S2pResult>` | `SessionError`、`SvsSingerNotFound`、`G2pNotImplementedError`、`S2pConversionFailed`(转换异常) |
| `validatePhonemes` | 检查 snapshot + 歌手 + capabilityReport 后逐音素比对 effective + reserved | `Expected<void>` | `SessionError`、`SvsSingerNotFound`、`G2pValidationError`(无 report / 空 effective / 出现不支持音素) |
| `createModelSet` | 检查 snapshot + 歌手 + resolutionState + Runtime，经 `SingerStageResolver::resolve` 构建 `ModelSet`，再封装为 `ModelSetHandle` | `Expected<shared_ptr<ModelSetHandle>>` | `SessionError`、`SvsSingerNotFound`、`SvsSingerNotLoaded`(非 Resolved)、`InferenceNotInitialized`(无 Runtime)、`SvsStageResolveFailed`(resolve 失败透传) |

---

### RefreshResult / ChangeSummary / PackageCoordinate

```cpp
// include/diffsinger/Session/VoicebankSession.h

struct DSSESSION_EXPORT PackageCoordinate {
    std::string packageId;
    stdc::VersionNumber version;
    bool operator==(const PackageCoordinate &) const = default;
};

struct DSSESSION_EXPORT ChangeSummary {
    std::vector<PackageCoordinate> added;      // 新增包
    std::vector<PackageCoordinate> removed;   // 移除包
    std::vector<PackageCoordinate> changed;    // 同 packageId 但 version 变化
    std::vector<PackageCoordinate> disabled;   // 由 Available/Degraded 跌至 Unavailable 的包
};

struct DSSESSION_EXPORT RefreshResult {
    bool succeeded = false;                       // 是否成功发布新快照
    bool coalesced = false;                       // 是否合并到既有 task(预留字段，当前总为 false)
    bool changed = false;                         // 新快照内容是否与上一份不同
    std::shared_ptr<const VoicebankSnapshot> snapshot;  // 成功=新快照；失败=旧快照
    ChangeSummary changes;                        // 相对上一份的 per-package delta
    std::vector<srt::core::Diagnostic> diagnostics;    // 刷新期收集的诊断
    std::vector<PackageCoordinate> updatesAvailable;   // 同坐标内容更新提示(预留，当前总为空)
    std::string errorMessage;                     // 失败时的简短消息
};
```

**字段语义**：
- `succeeded=false` 时，`snapshot` 仍是上一份未变更快照(或初始空快照)，`diagnostics` 携带失败原因(含每个非法包的诊断)。
- `changed` 基于内容比较(`contentEqual`)而非 generation 比较：第二次无变化刷新会返回 `changed=false`，Lite 可据此跳过冗余 UI 工作。
- `changes` 仅在 `changed=true` 且存在上一份快照时才计算；首次刷新 `changes` 各列表为空。
- `updatesAvailable` 为预留字段(未来用于同坐标内容热更新提示)，当前实现总是空。

---

### VoicebankSnapshot

```cpp
// include/diffsinger/Session/VoicebankSession.h
struct VoicebankSnapshot {
    std::vector<ds::bank::SingerSnapshot> singers;
    std::vector<ds::bank::PackageStatus> packages;
    std::vector<std::filesystem::path> roots;
    std::vector<std::string> reservedPhonemes;
    AvailabilitySummary availability;
    unsigned long long generation = 0;  // 单调递增，每次刷新 +1
};
```

**不可变性**：快照通过 `shared_ptr<const VoicebankSnapshot>` 发布，发布后所有字段均不可变。调用方持有共享指针可任意延长生命周期；session 内部写入新快照时不影响旧指针的读取。`generation` 仅用于 `ModelSetHandle` 的 staleness 比较，不参与 `contentEqual`。

---

### Availability 与 AvailabilitySummary

```cpp
// include/diffsinger/Session/VoicebankSession.h
enum class AvailabilityLevel { Available, Degraded, Unavailable };  // 内部三态

struct AvailabilitySummary {
    size_t available = 0;
    size_t degraded = 0;
    size_t unavailable = 0;
};

enum class Availability { Ready, Warning, Disabled };  // Lite-facing 三态
```

**三态语义**：
- `Ready`(内部 `Available`)：推理链完整、保留音素齐全、各一致性维度均无降级。
- `Warning`(内部 `Degraded`)：可用但存在能力缩减(任一 consistency 维度为 `Degraded`)或保留音素缺失(ROBUST-05：仍允许推理但提示)。
- `Disabled`(内部 `Unavailable`)：无法推理。触发条件：`resolutionState != Resolved`、`inferenceIds` 为空、`capabilityReport` 缺失、任一 consistency 维度为 `Inconsistent`、`effectivePhonemes` 为空。

`AvailabilitySummary` 是对快照中全部歌手的聚合计数。`capabilitySummary()` 返回单歌手的 `Availability`；`availability()` 返回聚合 `AvailabilitySummary`。

---

### SingerCapabilitySummary

```cpp
// include/diffsinger/Session/VoicebankSession.h
struct DSSESSION_EXPORT SingerCapabilitySummary {
    Availability availability = Availability::Disabled;
    std::vector<std::string> languages;          // effectiveLanguages
    std::vector<std::string> phonemes;           // effectivePhonemes
    std::vector<std::string> mixableSpeakers;   // mixableSpeakers
    std::vector<srt::core::Diagnostic> diagnostics;  // 含 phonemeWarnings/speakerWarnings/languageWarnings
};
```

**回退策略**：歌手存在但 `capabilityReport` 缺失时，回退到 `SingerSnapshot` 的扁平列表(`languages`/`speakerIds`)，`phonemes` 退化为 `reservedPhonemes`。歌手不存在时 `availability=Disabled` 且 `diagnostics` 携带 `SvsSingerNotFound`。无快照时直接返回 `Disabled` 不带诊断。

---

### S2pResult

```cpp
// include/diffsinger/Session/VoicebankSession.h
struct DSSESSION_EXPORT S2pResult {
    std::vector<std::string> phonemes;  // 音素序列
    std::vector<bool> onsets;            // 与 phonemes 平行：onsets[i] 表示 phonemes[i] 是否为某音节 onset 首音素
};
```

`phonemes` 与 `onsets` 长度相同。`onsets[i]==true` 表示 `phonemes[i]` 属于第 i 个音节的 onset 首音素。结果由 `LanguageResource::convert()` 产生的 `syllable.phonemes` / `syllable.onsets` 直接拷贝；转换异常被捕获并转为 `S2pConversionFailed` 错误。

---

### ModelSetHandle

```cpp
// include/diffsinger/Session/ModelSetHandle.h
namespace ds::session;

class DSSESSION_EXPORT ModelSetHandle {
public:
    ~ModelSetHandle();
    ModelSetHandle(const ModelSetHandle &) = delete;
    ModelSetHandle &operator=(const ModelSetHandle &) = delete;

    // 绑定的快照 generation
    unsigned long long snapshotGeneration() const noexcept;

    // 绑定的快照是否已被新快照替换(session 已推进 generation)
    bool isStale() const noexcept;

    // 惰性加载 stage(委托给 ModelSet::load)
    srt::core::Expected<srt::core::NO<srt::svs::Inference>> load(ds::infer::StageKind kind);

    // 启动 stage。stale 时返回 StaleModelSet，运行中任务不中断
    srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
        start(ds::infer::StageKind kind, const srt::core::NO<srt::core::TaskStartInput> &input);

    // 取上次 start 的结果(若无则返回空 NO)
    srt::core::NO<srt::core::TaskResult> result(ds::infer::StageKind kind) const;

    // stop + 释放模型并清空 retained result
    srt::core::Expected<void> reset(ds::infer::StageKind kind);

    // 仅停止推理不释放模型(委托 ModelSet::stop)
    srt::core::Expected<void> stop(ds::infer::StageKind kind);

    // 释放模型(先 stop 后释放)
    srt::core::Expected<void> unload(ds::infer::StageKind kind);

    // 逆序卸载全部(vocoder → acoustic → variance → pitch → duration)
    srt::core::Expected<void> unloadAll();

    bool isLoaded(ds::infer::StageKind kind) const noexcept;

    /// 底层 StageSet（委托 ModelSet::stages()）。让宿主读取每 stage 的
    /// InferenceImportOptions 而无需重新走 SingerStageResolver。
    /// 在 handle 生命周期内稳定。
    const ds::infer::StageSet &stages() const noexcept;
};
```

**StaleModelSet 语义**：
- `createModelSet` 绑定当前快照 generation，构造时传入 `isCurrentGenerationFn` 回调(捕获 session `Impl` 的 `weak_ptr`)。
- 刷新成功后 session 内部 generation 推进，旧 handle 的 `isStale()` 返回 `true`。
- **只有 `start()` 在 stale 时拒绝**(`ErrorCode::StaleModelSet`)；`load`/`stop`/`unload`/`unloadAll`/`result`/`isLoaded` 允许执行——已运行任务可在旧模型上自然结束，Lite 不需要立即丢弃旧 handle。
- session 销毁后 `weak_ptr` 失效，回调返回 `false`，handle 自动报告 stale。

---

## 典型调用流程

```cpp
#include <diffsinger/Session/VoicebankSession.h>
#include <diffsinger/Session/ModelSetHandle.h>
#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/G2P/LanguageService.h>
#include <synthrt/Driver/OnnxInferenceDriver.h>

// 1. 创建 session 并配置
ds::session::VoicebankSession session;
session.setRoots(voicebankPaths);
session.setReservedPhonemes({"SP", "AP"});

// 初始化并注入 LanguageService(由调用方负责 initialize)
auto langSvc = std::make_shared<srt::g2p::LanguageService>();
langSvc->initialize(g2pPluginPaths, officialG2pPaths, packageDirs);
session.setLanguageService(langSvc);

// 初始化并注入 Runtime(由调用方负责 loadPackage)
srt::core::Runtime runtime;
srt::driver::setupOnnxInferenceDriver(runtime, pluginRoot, onnxConfig);
session.setRuntime(&runtime);

// 2. 刷新声库(同 session 并发 refreshAsync 合并为同一 task)
auto task = session.refreshAsync();
auto result = task.get();
if (!result.succeeded) {
    // result.snapshot 仍是旧快照(或空)，result.errorMessage 说明原因
    return;
}
auto snap = result.snapshot;          // shared_ptr<const VoicebankSnapshot>
if (result.changed) {
    // 处理 changes.added/removed/changed/disabled，更新 UI
}

// 3. 查询能力
ds::bank::SingerRef ref("pkg", "singer", "1.0.0");
auto summary = session.capabilitySummary(ref);
if (summary.availability == ds::session::Availability::Ready) {
    // summary.languages / phonemes / mixableSpeakers 可用于 UI
} else if (summary.availability == ds::session::Availability::Warning) {
    // 查 summary.diagnostics 看降级原因
}

// 4. G2P / S2P 转换(C++ 同步 Expected；Lite 自行决定异步调度)
std::vector<srt::g2p::G2pInput> g2pInputs{ /* ... */ };
auto g2pExp = session.convertG2p(ref, "cmn", g2pInputs);
if (!g2pExp) {
    // g2pExp.error() 可能是 SessionError / SvsSingerNotFound / G2pNotImplementedError
    return;
}
auto s2pExp = session.convertS2p(ref, "cmn", "ni3");
if (!s2pExp) {
    // S2pConversionFailed 等
    return;
}
auto &s2p = *s2pExp;  // phonemes + onsets

// 5. 最终音素校验(S2P 后、+ note 分配、手工修改后调用)
auto validExp = session.validatePhonemes(ref, finalPhonemes);
if (!validExp) {
    // G2pValidationError: 必须阻断下游推理，保留编辑数据
    return;
}

// 6. 加载歌手包到 Runtime(必须先于 createModelSet)
runtime.loadPackage(snap->packages[0].rootPath);  // 或按 ref.packageId 查表

// 7. 创建 ModelSet 并推理
auto modelExp = session.createModelSet(ref);
if (!modelExp) {
    // SessionError / SvsSingerNotFound / SvsSingerNotLoaded
    // InferenceNotInitialized / SvsStageResolveFailed
    return;
}
auto handle = *modelExp;
handle->load(ds::infer::StageKind::Duration);
auto startExp = handle->start(ds::infer::StageKind::Duration, durationInput);
if (!startExp) {
    // startExp.error() 可能是 StaleModelSet(见下)
    return;
}
auto taskResult = handle->result(ds::infer::StageKind::Duration);
// 用 taskResult->as<...>() 读取结果

// 8. 刷新成功后旧 handle 变 stale
auto result2 = session.refreshAsync().get();
if (result2.succeeded && result2.changed) {
    // 旧 handle->start() 现在返回 StaleModelSet
    // load/stop/unload 仍允许，已运行任务可结束
    // Lite 仅对尚未开始的 task：丢弃旧 handle、重新 createModelSet、重试一次
    auto newHandle = *session.createModelSet(ref);
    newHandle->load(ds::infer::StageKind::Duration);
    newHandle->start(ds::infer::StageKind::Duration, durationInput);
}

// 9. 卸载(显存优化可单独卸载 vocoder，保留 acoustic)
handle->unload(ds::infer::StageKind::Vocoder);
handle->unloadAll();  // 逆序卸载全部
```

---

## 原子刷新语义

参考 vnext 01 与 04 文档：

- **并发合并**：`refreshAsync` 检查 `_impl->inFlight` 是否未完成；若是则直接返回同一个 `shared_future`，避免并发刷新重复扫描。
- **整体发布**：成功路径在 `_impl->mutex` 下一次性替换 `current` 快照并推进 `generation`；调用方拿到的是整体一致的 `VoicebankSnapshot`。
- **失败保留旧快照**：任一包非法(`!package.valid`)、scanner 失败或抛异常时，`succeeded=false`，`snapshot` 仍是上一份未变更快照(可能为空)；`diagnostics` 携带失败包诊断，`errorMessage` 说明原因。Lite 不会看到半成品快照。
- **刷新中任务用旧资源**：刷新开始后当前 snapshot 和已有 ModelSet 不变；新提交的 G2P/推理继续使用当前快照。成功发布新快照后，新任务才进入新资源。
- **changed 反映内容差异**：基于 `contentEqual`(roots、reservedPhonemes、packages 的 packageId+version+valid、singers 的 ref+resolutionState+inferenceIds)而非 generation。首次刷新无上一份时 `changed=true`；无变化的二次刷新 `changed=false`。
- **updatesAvailable 预留**：当前实现总是空，未来用于同坐标内容热更新提示。

---

## StaleModelSet 语义

参考 vnext 03 文档：

- **绑定 generation**：`createModelSet` 创建 `ModelSetHandle` 时绑定 `snap->generation`，并传入 `isCurrentGenerationFn` 回调(捕获 `weak_ptr<Impl>`)。
- **stale 判定**：handle 调用 `isStale()` 时通过回调检查 `sp->generation == gen`；session 销毁后 `weak_ptr` 失效，回调返回 `false`，handle 自动 stale。
- **start 拒绝 stale**：`handle->start()` 在 `isStale()==true` 时返回 `ErrorCode::StaleModelSet`。
- **load/stop/unload 允许 stale**：刷新成功后旧 handle 的 `load`/`stop`/`unload`/`unloadAll`/`result`/`isLoaded` 仍可执行，让已运行任务在旧模型上自然结束。
- **Lite 重试策略**：仅对尚未开始的 task 丢弃旧缓存、重新 `createModelSet()`、重试一次；已开始 task 不自动在新模型重跑。Lite 必须按 `(singerKey, stage)` 串行 `start/result/stop`。

---

## 错误处理

错误码段定义于 `include/synthrt/Core/Support/Diagnostic.h`。session 模块相关错误码：

| 错误码 | 数值段 | 产生场景 |
|---|---|---|
| `SessionError` | 0-99 General | `convertG2p`/`convertS2p`/`validatePhonemes`/`createModelSet` 在无快照时返回 |
| `InferenceNotInitialized` | 200-299 | `createModelSet` 在 `setRuntime(nullptr)` 后返回 |
| `StaleModelSet` | 200-299 | `ModelSetHandle::start` 在 stale handle 上返回 |
| `ModelBusy` | 200-299 | C ABI 同 model 忙时返回(C++ 同步 API 不产生) |
| `G2pNotImplementedError` | 300-399 | `convertG2p`/`convertS2p` 在 `setLanguageService(nullptr)` 后返回 |
| `G2pValidationError` | 300-399 | `validatePhonemes` 在无 capabilityReport / 空 effective / 出现不支持音素时返回 |
| `S2pConversionFailed` | 500-599 | `convertS2p` 在 `LanguageResource::convert` 抛异常时返回 |
| `SvsSingerNotFound` | 600-699 | 各查询在 `findSinger` 返回 nullptr 时返回 |
| `SvsSingerNotLoaded` | 600-699 | `createModelSet` 在 `resolutionState != Resolved` 时返回 |
| `SvsStageResolveFailed` | 600-699 | `createModelSet` 在 `SingerStageResolver::resolve` 失败时透传 |

**错误工厂**：`SvsSingerNotFound`/`SvsSingerNotLoaded`/`InferenceNotInitialized`/`G2pValidationError` 等 inference 类错误使用 `srt::core::Error::inferenceError()` 工厂，自动填充 `singerId` 上下文。`SessionError`/`G2pNotImplementedError`/`S2pConversionFailed` 使用裸 `Error(code, message)` 构造。

**关键约束(ROBUST-05)**：`validatePhonemes` 不静默接受不支持音素、不复制 lyric、不写空音素、不自动替换 token；失败必须保留编辑数据并阻断下游推理。

---

## 测试

单元测试位于 `domains/ds-session/unittests/test_voicebank_session.cpp`，使用 Catch2 v3，tag `[ds-session]`。因不加载插件 DLL，主要覆盖错误路径与快照状态；成功路径(G2P/S2P/真实 ONNX 模型)和 staleness-after-refresh 路径留待 L2 测试。

| 测试用例 | 覆盖合同 |
|---|---|
| publishes immutable snapshot from concurrent refresh calls | WP0：并发 `refreshAsync` 合并、snapshot 原子发布、`availability` 聚合计数 |
| preserves previous snapshot after invalid package | WP0：单包非法中止刷新，旧快照保留 |
| refresh marks changed=true on first refresh and false when unchanged | WP0：`changed` 基于内容比较、无变化时为 false |
| refresh reports added package in changes summary | WP0：`changes.added` 正确填充 |
| refresh collects diagnostics for invalid packages | WP0：非法包诊断返回给调用方 |
| refresh keeps updatesAvailable empty (future capability) | WP0：预留字段当前为空 |
| capabilitySummary returns Disabled when snapshot is empty | WP3：无快照时 Disabled |
| capabilitySummary returns Disabled for unknown singer | WP3：未知歌手 Disabled + `SvsSingerNotFound` 诊断 |
| convertG2p returns error when no LanguageService is configured | WP3：无 LanguageService → `G2pNotImplementedError` |
| convertG2p returns error when singer not found | WP3：未知歌手 → `SvsSingerNotFound` |
| convertS2p returns error without LanguageService | WP3：S2P 无 LanguageService → `G2pNotImplementedError` |
| validatePhonemes returns error when singer not found | WP3：未知歌手 → `SvsSingerNotFound` |
| validatePhonemes blocks on empty capability report | WP3/ROBUST-05：无 report → `G2pValidationError`，不静默接受 |
| validatePhonemes returns error when snapshot is empty | WP3：无快照 → `SessionError` |
| setRuntime/runtime round-trip stores and returns the pointer | WP4：Runtime 注入/读取 round-trip |
| createModelSet returns SessionError when snapshot is empty | WP4：无快照 → `SessionError` |
| createModelSet returns SvsSingerNotFound for unknown singer | WP4：未知歌手 → `SvsSingerNotFound` |
| createModelSet returns InferenceNotInitialized when no Runtime is configured | WP4：无 Runtime → `InferenceNotInitialized` |
| createModelSet returns error when Runtime has no singer package loaded | WP4：Runtime 未 `loadPackage` → resolve 失败透传 |
