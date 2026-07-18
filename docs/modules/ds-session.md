# DS Session 模块 (`ds::session`)

namespace: `ds::session` | target: `srt-ds::session` | 头文件: `include/diffsinger/Session/`

## 定位

`VoicebankSession` 是宿主使用声库、G2P/S2P 和模型集的统一入口。它组合 `VoicebankScanner`、`LanguageService`、`Runtime`、依赖解析、快照切换和旧资源排空。Lite 不直接持有 route、Runtime 或 G2P manager。

D-26 ~ D-36 / V3 重构落地后，Session 采用 Discovery/On-demand 两阶段加载：discovery 阶段只扫描目录、解析清单、校验完整性（仅元数据）；on-demand 阶段由 `ensureLanguageReady()` / `ensureModelSet()` 同步加载 G2P 字典与模型权重（D-27/D-34）。Session **借入** Runtime 和 LanguageService（不拥有其生命周期），bare `setRuntime()` / `setLanguageService()` 已标记 `[[deprecated]]`，由 `VoicebankSession(SessionResources)` 构造函数取代（D-27/K-01）。

## 关键 API

```cpp
// include/diffsinger/Session/VoicebankSession.h
namespace ds::session;

struct SessionResources {
    srt::core::Runtime *runtime = nullptr;                        // createModelSet 必需
    std::shared_ptr<srt::g2p::LanguageService> languageService;   // convertG2p/convertS2p 必需
};

class VoicebankSession {
public:
    VoicebankSession();
    explicit VoicebankSession(SessionResources resources);          // 资源注入 (V3-06 / WP3)
    ~VoicebankSession();

    // 配置（下次 refresh 生效，D-35）
    void setRoots(std::vector<std::filesystem::path> roots);
    void setReservedPhonemes(std::vector<std::string> phonemes);

    // 已弃用的 bare setter — 等价于 SessionResources 注入后调用对应 getter
    [[deprecated("Use VoicebankSession(SessionResources). Will be removed in Level=3.")]]
    void setLanguageService(std::shared_ptr<srt::g2p::LanguageService> service);
    [[deprecated("Use VoicebankSession(SessionResources). Will be removed in Level=3.")]]
    void setRuntime(srt::core::Runtime *runtime);

    // 刷新：同步为首选（D-29）。并发 refresh 共享一次扫描。
    RefreshResult refresh();                                         // 同步，Lite worker 调用
    std::shared_future<RefreshResult> refreshAsync();                // 异步，CLI/tests/C ABI 用
    RefreshSubscription subscribeRefresh(std::function<void(const RefreshResult &)> callback);

    std::shared_ptr<const VoicebankSnapshot> snapshot() const;
    AvailabilitySummary availability() const;
    SingerCapabilitySummary capabilitySummary(const ds::bank::SingerRef &singerKey) const;

    // G2P / S2P / 音素校验（V3-10：通过 version-aware LanguageService 路由）
    srt::core::Expected<std::vector<srt::g2p::G2pRes>>
        convertG2p(const ds::bank::SingerRef &singerKey,
                   const std::string &language,
                   const std::vector<srt::g2p::G2pInput> &inputs) const;
    srt::core::Expected<S2pResult>
        convertS2p(const ds::bank::SingerRef &singerKey,
                   const std::string &language,
                   const std::string &pronunciation) const;
    srt::core::Expected<void>
        validatePhonemes(const ds::bank::SingerRef &singerKey,
                         const std::vector<std::string> &phonemes) const;

    // On-demand 资源加载 (V3-08 / WP4)
    srt::core::Expected<void> ensureLanguageReady(
        const std::string &packageId,
        const stdc::VersionNumber &version,
        const std::string &language);
    srt::core::Expected<std::shared_ptr<ModelSetHandle>>
        ensureModelSet(const ds::bank::SingerRef &singerKey);

    // ModelSet 句柄（绑定当前 snapshot generation）
    srt::core::Expected<std::shared_ptr<ModelSetHandle>>
        createModelSet(const ds::bank::SingerRef &singerKey);
};
```

C ABI / CLI 等 headless host 使用默认构造函数，仅走 discovery 路径（D-27 例外、D-36/K-10）。资源完备的 session 通过 `VoicebankSession(SessionResources)` 或 C ABI 的 `srt_session_create_with_resources` 构造。

## VoicebankSnapshot 与 ChangeSummary

```cpp
struct VoicebankSnapshot {
    std::vector<ds::bank::SingerSnapshot> singers;
    std::vector<ds::bank::PackageStatus> packages;
    std::vector<std::filesystem::path> roots;
    std::vector<std::string> reservedPhonemes;
    AvailabilitySummary availability;
    unsigned long long generation = 0;

    // V3-07 fingerprint (D-33 / WP2)
    std::string catalogFingerprint;    // 全量目录哈希，匹配 Lite PackageCatalog::generation
    std::string languageFingerprint;   // G2P/S2P 路由元数据哈希
};

struct ChangeSummary {
    std::vector<PackageCoordinate> added;
    std::vector<PackageCoordinate> removed;
    std::vector<PackageCoordinate> changed;    // 同坐标但元数据/能力变化
    std::vector<PackageCoordinate> disabled;
};

struct RefreshResult {
    bool succeeded = false;
    bool coalesced = false;
    bool changed = false;
    std::shared_ptr<const VoicebankSnapshot> snapshot;
    ChangeSummary changes;
    std::vector<srt::core::Diagnostic> diagnostics;
    std::vector<PackageCoordinate> updatesAvailable;   // 同坐标内容变化，提示用户重载/迁移
    std::string errorMessage;
};
```

`PackageCoordinate` 由 `(packageId, stdc::VersionNumber)` 组成，D-24 多版本共存场景下不同 version 各占一行。

## 刷新与订阅

- `refresh()` 同步扫描并返回 `RefreshResult`，是 Lite worker 线程的首选入口（D-29/K-03）；`refreshAsync()` 返回 `shared_future`，供 CLI/tests/C ABI 等待。
- `subscribeRefresh(callback)` 返回 RAII 订阅句柄；析构自动退订，可在 callback 内安全销毁。Lite adapter 对 session 做一次长期订阅，将 C++ worker callback 以 Qt queued invocation 投递给 PackageManager GUI 线程。
- 新 refresh 协作取消旧扫描，并自动以最新配置执行；中间 Cancelled 不通知 Lite UI，只发布最终结果。
- 成功时一次性发布新的不可变 snapshot；失败/取消/超时保留旧 snapshot。
- 刷新扫描中，所有新 G2P/推理继续从旧已发布 snapshot 获取资源。

### Discovery 阶段部分成功 (D-31 / K-05)

一次 `refresh()` 扫描多个包，各自独立：

- **合法包**：发布完整元数据到 `snapshot.packages` / `snapshot.singers`。
- **损坏包**：发布带 `diagnostics` 的 `PackageStatus`（`valid=false`），不进入 `singers`。
- 不做"全或全不"的原子性拒绝；`RefreshResult.succeeded` 仅在扫描整体不可恢复（如根目录不存在）时为 `false`。
- `RefreshResult.changes.disabled` 去重，避免同一包重复列入（b932eec 修复）。

## ModelSetHandle 与 stale

`createModelSet(singerKey)` 自动确保目标 package 已加载，并返回绑定当前 snapshot generation 的 `ModelSetHandle`。实例非重入；同一 stage 的 `start/result/stop` 必须串行。

```cpp
// include/diffsinger/Session/ModelSetHandle.h
class ModelSetHandle {
public:
    unsigned long long snapshotGeneration() const noexcept;
    bool isStale() const noexcept;        // 绑定 generation 与当前 generation 比较

    srt::core::Expected<srt::core::NO<srt::svs::Inference>> load(ds::infer::StageKind kind);
    srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
        start(ds::infer::StageKind kind, const srt::core::NO<srt::core::TaskStartInput> &input);
    srt::core::NO<srt::core::TaskResult> result(ds::infer::StageKind kind) const;
    srt::core::Expected<void> reset(ds::infer::StageKind kind);   // stop + 释放保留的 result
    srt::core::Expected<void> stop(ds::infer::StageKind kind);
    srt::core::Expected<void> unload(ds::infer::StageKind kind);
    srt::core::Expected<void> unloadAll();
    bool isLoaded(ds::infer::StageKind kind) const noexcept;
    const ds::infer::StageSet &stages() const noexcept;            // b39075a 暴露
};
```

`start()` 是唯一会因 stale 拒绝的操作：刷新发布新内容后，对旧 handle 调用 `start()` 返回 `ErrorCode::StaleModelSet`（D-30 / K-04）。

- 已进入 `start()` 的任务继续使用旧模型完成；
- 尚未开始的任务收到 `StaleModelSet`，宿主只允许重建并重试一次（D-30）；
- `load/stop/unload/unloadAll` 在 stale handle 上仍允许（已运行任务可结束、显存可释放）；
- 首次实际模型加载失败会将对应 singer 标记 `Disabled` 并通过后续 Refresh/availability 事件通知宿主。

## ensure\* 同步 API (V3-08 / WP4 / D-34)

```cpp
// 显式确保 G2P + S2P 已就绪；同步阻塞、幂等。空 version 触发 G2pVersionAmbiguous。
Expected<void> ensureLanguageReady(packageId, version, language);

// 等价于 createModelSet，但按 D-34 显式分类错误：NotFound / VersionAmbiguous /
// LoadFailed / StaleModelSet / RuntimePackageNotLoaded
Expected<std::shared_ptr<ModelSetHandle>> ensureModelSet(singerKey);
```

错误码（V3-09 / V3-10，追加在 Inference 段末尾，ARCH-02 仅追加不重排）：

| ErrorCode | 数值 | 触发场景 |
|---|---|---|
| `StaleModelSet` | 216 | handle 绑定的 generation 已被新 snapshot 取代 |
| `LoadFailed` | 217 | ONNX session 创建/加载失败 |
| `RuntimePackageNotLoaded` | 218 | `Runtime::loadPackage()` 未对 singer 的 package 调用 |
| `G2pVersionAmbiguous` | 321 | packageId 注册多个 version 但调用方未传 version |
| `SvsSingerAmbiguous` | 601 | singerId 匹配多个 singer 但调用方未传 version |

## 语言、音素与混音

Session 直接提供 `convertG2p`、`convertS2p` 与 `validatePhonemes`。失败返回结构化 `Expected` 错误，不执行 copy fallback、不写空音素、不自动替换 token。

V3-10 起三个方法全部通过 **version-aware** `LanguageService` 重载路由：

- `convertG2p` 调用 `LanguageService::convert(packageId, version, singerId, languageId, inputs)`（V3-01 / WP5）。
- `convertS2p` 调用 `LanguageService::resolveS2pResource(packageId, version, singerId, languageId)`（WP5，2018abb 修复 legacy 3-arg 路由在多版本场景下不精确）。
- version 取自 `SingerRef.version`；空 version 在多版本注册的 packageId 上触发 `G2pVersionAmbiguous`，单版本场景透明路由（向后兼容）。

降级行为：

- G2P 失败：宿主保留 lyric 与已编辑 pronunciation，标记 note 错误。
- S2P 失败：保留既有 phoneme/offset，标记 note 错误。
- 手工 token、SP/AP 和 `+` 分配后的最终 token 不合法：仅阻断所在 piece 的后续推理。
- mapping/embedding/mix 错误：保留用户 mix，标错误，不自动删除 source 或回退单音色。
- 某 G2P module 初始化失败：仅关联 singer-language `Disabled`，其他语言/声库仍可发布。

## findSinger 多版本歧义拒绝 (D-42)

`VoicebankSession` 内部 `findSinger(snapshot, key)` 返回 `Expected<const SingerSnapshot *>`：

- 0 匹配 → `SvsSingerNotFound`
- >1 匹配且 `key.version` 为空 → `SvsSingerAmbiguous`（复用 D-41 错误码，与 `SingerStageResolver` 镜像）
- 恰好 1 匹配 → 返回 singer 指针

5 个调用点（`capabilitySummary` / `convertG2p` / `convertS2p` / `validatePhonemes` / `createModelSet`）全部改为处理 `Expected`，错误时显式构造 `Diagnostic`（携带 `packageId` + `singerId`），禁止继续使用错误 singer。这构成多版本歧义的两层防护：snapshot 层先拒绝、stage 层兜底（D-41/D-42 联动）。

## Lite-facing 摘要

宿主只消费 `Ready`、`Warning`、`Disabled` 与 diagnostics。PackageManager 显示包级状态；展开 singer 显示三态和简短原因；详情页显示完整 diagnostics。相同 package ID/version 但内容变化时（`updatesAvailable`）提示用户重载/迁移，绝不自动切换项目绑定。

## 缓存

推理缓存使用完整运行指纹：snapshot identity、精确 singer、stage、保留 token、provider/device、请求输入、输出格式/采样率。变化自动产生新 key；旧缓存由 Lite 空闲时 LRU 回收（K-09 之外，LRU 回收策略不在本轮范围）。
