# DS Session 模块 (`ds::session`)

namespace: `ds::session` | target: `srt-ds::session` | 头文件: `include/diffsinger/Session/`

## 定位

`VoicebankSession` 是宿主使用声库、G2P/S2P 和模型集的统一入口。它组合 `VoicebankScanner`、`LanguageService`、`Runtime`、依赖解析、快照切换和旧资源排空。Lite 不直接持有 route、Runtime 或 G2P manager。

**本地化（本轮改造）**：`VoicebankSession::setDisplayLocale()` 及内部 locale 传播链已整条移除。snapshot 中的  `SingerSnapshot::name` / `PackageManifest` 各文本字段均为 `srt::core::DisplayText`，携带 BCP 47 全翻译；宿主切 UI 语言时对缓存值 `text(locale)` 重新取词即可，**不需要再调用 refresh() 重扫声库**（指纹也不受显示语言影响，见下 §VoicebankSnapshot）。

D-26 ~ D-36 / V3 重构落地后，Session 采用 Discovery/On-demand 两阶段加载：discovery 阶段只扫描目录、解析清单、校验完整性（仅元数据）；on-demand 阶段由 `ensureLanguageReady()` / `ensureModelSet()` 同步加载 G2P 字典与模型权重（D-27/D-34）。Session **借入** Runtime 和 LanguageService（不拥有其生命周期），bare `setRuntime()` / `setLanguageService()` 已标记 `[[deprecated]]`，由 `VoicebankSession(SessionResources)` 构造函数取代（D-27/K-01）。

## 关键 API

```cpp
// include/diffsinger/Session/VoicebankSession.h
namespace ds::session;

struct SessionResources {
    srt::core::Runtime *runtime = nullptr;                        // createModelSet 必需
    std::shared_ptr<srt::g2p::LanguageService> languageService;   // convertG2p/convertS2p 必需
    /// G2P 插件搜索路径。非空时随 refresh() 传入 initializeMetadata/updateMetadata。
    /// **v7 勘误**：自动初始化触发条件为 `languageService != nullptr`（`if (svc)`），而非 `g2pPluginPaths` 非空。
    std::vector<std::filesystem::path> g2pPluginPaths;
    /// 官方 G2P 包路径（如 resources/G2pPackages/），随 g2pPluginPaths 一并传入。
    std::vector<std::filesystem::path> officialG2pPackages;
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
    /// TD-01 (D-39 #2): full manifests for valid packages, ordered by
    /// discovery (same order as `packages` filtered to valid entries).
    /// Lets lite PackageManager read author/description/license/singer/
    /// speaker/language detail directly from the snapshot without keeping
    /// a separate PackageCatalog. Invalid packages contribute only their
    /// PackageStatus.error; they have no manifest here.
    std::vector<ds::bank::PackageManifest> manifests;
    std::vector<std::filesystem::path> roots;
    std::vector<std::string> reservedPhonemes;
    AvailabilitySummary availability;
    unsigned long long generation = 0;

    // V3-07 fingerprint (D-33 / WP2)
    std::string catalogFingerprint;    // 全量目录哈希，匹配 Lite PackageCatalog::generation
    std::string languageFingerprint;   // G2P/S2P 路由元数据哈希
    // 注：SingerSnapshot.name 现为 DisplayText，其 fingerprint 序列化默认文本 + 全部
    // "locale=text" 对；因此编辑任意翻译会改变指纹（→ 包 changed），而切换 UI 显示语言
    // 不再改变指纹（→ 不触发重扫/重建缓存）。
    // 已知限制：指纹经 DisplayText::text(tag) 取值，POSIX 写法键（含 '_'，如 zh_CN）
    // 的译文不参与 Lookup、会序列化为默认文本——不合规键的译文变更检测不到。由于该类
    // 键本就无效（PackageValidator 报 Error），不修 DisplayText；数据作者应迁移为
    // BCP 47 键。
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
| `SvsSingerAmbiguous` | 604 | singerId 匹配多个 singer 但调用方未传 version |

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

---

## VoicebankSnapshot 查询便捷方法 (A2)

> A3 追加（2026-07-25）。详细设计见
> [docs/lite-integration/02-synthrt-side-changes.md](file:///d:/projects/synthrt/docs/lite-integration/02-synthrt-side-changes.md) §A2。

`VoicebankSnapshot` 提供 4 个 const 查询方法，供宿主快速定位 singer / package /
manifest，避免在调用方重复实现 O(n) 遍历。这些方法**不引入索引 map**：snapshot 是
不可变的，调用方频繁时可在调用方层加缓存。完整签名见
[include/diffsinger/Session/VoicebankSession.h](file:///d:/projects/synthrt/include/diffsinger/Session/VoicebankSession.h)。

```cpp
struct VoicebankSnapshot {
    // ... 现有字段不变 ...

    /// 精确 (packageId, singerId, version) 匹配。未找到返回 nullptr。
    const ds::bank::SingerSnapshot *findSinger(const ds::bank::SingerRef &ref) const;

    /// 仅按 singerId 匹配，多版本同 packageId 场景可能返回多个。
    std::vector<const ds::bank::SingerSnapshot *>
        findSingersBySingerId(const std::string &singerId) const;

    /// (packageId, version) 匹配 PackageStatus。invalid 包仍在 `packages` 中
    /// （valid=false），本方法同样返回它们，由调用方检查 `valid` 字段。
    const ds::bank::PackageStatus *
        findPackage(const std::string &packageId,
                    const stdc::VersionNumber &version) const;

    /// (packageId, version) 匹配 PackageManifest。invalid 包无 manifest 条目
    /// （TD-01），返回 nullptr。
    const ds::bank::PackageManifest *
        findManifest(const std::string &packageId,
                     const stdc::VersionNumber &version) const;
};
```

### 语义要点

- **返回值**：所有方法返回 raw pointer（或 `vector<const T*>`），**非 owning**，
  仅在 snapshot 存活期间有效。宿主持有 `shared_ptr<const VoicebankSnapshot>` 即可
  保证指针有效。
- **线程安全**：snapshot 在 `refresh()` 成功后**不可变**发布，这 4 个 const 方法
  内部仅做线性遍历，可在多线程并发调用（A2-T11 验证）。
- **版本比较规范化**：`findSinger` / `findPackage` / `findManifest` 的 version
  比较通过 `stdc::VersionNumber::operator==` 自动规范化——`"1.0"` 与 `"1.0.0"`
  视为等价；空 version 仅匹配空 version（A2-T02）。
- **invalid 包行为**：`findPackage` 对 invalid 包仍返回 `PackageStatus*`（调用方
  检查 `valid` 字段）；`findManifest` 对 invalid 包返回 `nullptr`（invalid 包无
  manifest 条目，TD-01）。
- **与 `VoicebankSession::findSinger` 的关系**：`VoicebankSession` 内部私有
  `findSinger(snapshot, key)` 是会话层的多版本歧义拒绝逻辑（返回
  `Expected<const SingerSnapshot*>`，0 匹配 → `SvsSingerNotFound`，>1 匹配且空
  version → `SvsSingerAmbiguous`，见上文 §"findSinger 多版本歧义拒绝 (D-42)"）。
  A2 的 `VoicebankSnapshot::findSinger` 是 snapshot 层的精确匹配查询，不做歧义
  拒绝，由调用方自行决定策略。

### 验证

参见 [docs/lite-integration/05-verification-checklist.md](file:///d:/projects/synthrt/docs/lite-integration/05-verification-checklist.md) §A2（A2-T01 ~ A2-T11）。

---

## VoicebankSession 移动语义 (A2 / B1a)

> A3 追加（2026-07-25）。

`VoicebankSession` 是 **move-only** 类型：

```cpp
class VoicebankSession {
public:
    VoicebankSession(const VoicebankSession &) = delete;
    VoicebankSession &operator=(const VoicebankSession &) = delete;
    VoicebankSession(VoicebankSession &&) noexcept;          // move ctor
    VoicebankSession &operator=(VoicebankSession &&) noexcept; // move assign
    // ...
};
```

实现由 `shared_ptr<Impl>` 承载，移动后源对象的 Impl 为空、可安全析构。该语义允许
宿主在 `initialize()` 中**重新构造** `m_session` 成员——典型场景是 ds-editor-lite
的 `SynthrtEngine`：构造时 `pluginRoot()` 尚不可用，只能默认构造一个空 session；
待 `pluginRoot()` 就绪后再 `m_session = VoicebankSession(SessionResources{...})`
注入资源（[docs/lite-integration/03-lite-side-migration.md](file:///d:/projects/synthrt/docs/lite-integration/03-lite-side-migration.md) B1a）。

约束：

- 移动后对源对象的任何非析构调用是未定义行为；宿主应在移动后立即丢弃源对象。
- 移动操作 noexcept，可在 noexcept 容器中使用。
- 不支持拷贝：`VoicebankSession` 持有 refresh 订阅、扫描状态等不可共享资源。

