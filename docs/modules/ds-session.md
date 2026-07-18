# DS Session 模块 (`ds::session`)

namespace: `ds::session` | target: `srt-ds::session` | 头文件: `include/diffsinger/Session/`

## 定位

`VoicebankSession` 是宿主使用声库、G2P/S2P 和模型集的唯一公开入口。它隐藏 Runtime、LanguageService、扫描器、依赖解析、快照切换和旧资源排空。Lite 不直接持有 route、Runtime 或 G2P manager。

当前已实现的注入式接口是过渡形态；最终公开接口改为 Session 自管配置与资源。

## 最终易用接口

```cpp
VoicebankSession session(SessionConfig{
    .voicebankRoots = voicebankRoots,
    .g2pRoots = g2pRoots,
    .reservedPhonemes = {"SP", "AP"},
    .driver = driverConfig,
});

auto subscription = session.subscribeRefresh(
    [](const RefreshResult &result) {
        // SynthRT worker thread callback: host dispatches to its UI thread.
    });

session.refreshAsync();              // CLI/tests may also wait on its future
const auto snapshot = session.snapshot();
auto models = session.createModelSet(singerKey); // internally loads package
```

构造只保存配置；首次和后续 `refreshAsync()` 在后台初始化/重载 Runtime、G2P、包图和快照。Session 析构会停止新订阅与未开始任务；已进入模型 `start()` 的任务持有资源直到结束，析构等待其排空。

## 刷新与订阅

`refreshAsync()` 返回 future，供 CLI、测试和 C ABI 等待；Lite 不在 UI 线程等待它。Lite adapter 对 session 做一次长期订阅，将 C++ worker callback 以 Qt queued invocation 投递给 PackageManager GUI 线程。

- `subscribeRefresh(callback)` 返回 RAII 订阅句柄；析构自动退订，可在 callback 内安全销毁。
- 新 refresh 协作取消旧扫描，并自动以最新配置执行；中间 Cancelled 不通知 Lite UI，只发布最终结果。
- 成功时一次性发布新的不可变 snapshot；失败/取消/超时保留旧 snapshot。
- 刷新扫描中，所有新 G2P/推理继续从旧已发布 snapshot 获取资源。
- `RefreshResult` 包含 snapshot、changed、added/removed/changed/disabled 摘要、diagnostics 与同坐标内容更新提示。

## 模型与 stale

`createModelSet(singerKey)` 自动确保目标 package 已加载，并返回绑定当前 snapshot 的模型集。实例非重入；同一 stage 的 `start/result/stop` 必须串行。

刷新发布新内容后：

- 已进入 `start()` 的任务继续使用旧模型完成；
- 尚未开始的任务收到 `StaleModelSet`，宿主只允许重建并重试一次；
- 任务写回结果时必须同时匹配创建时 snapshot identity 与宿主编辑 revision，否则丢弃；
- 首次实际模型加载失败会将对应 singer 标记 `Disabled` 并通过后续 Refresh/availability 事件通知宿主。

## 语言、音素与混音

Session 直接提供 `convertG2p`、`convertS2p` 与 `validatePhonemes`。失败返回结构化诊断，不执行 copy fallback、不写空音素、不自动替换 token。

- G2P 失败：宿主保留 lyric 与已编辑 pronunciation，标记 note 错误。
- S2P 失败：保留既有 phoneme/offset，标记 note 错误。
- 手工 token、SP/AP 和 `+` 分配后的最终 token 不合法：仅阻断所在 piece 的后续推理。
- mapping/embedding/mix 错误：保留用户 mix，标错误，不自动删除 source 或回退单音色。
- 某 G2P module 初始化失败：仅关联 singer-language `Disabled`，其他语言/声库仍可发布。

## Lite-facing 摘要

宿主只消费 `Ready`、`Warning`、`Disabled` 与 diagnostics。PackageManager 显示包级状态；展开 singer 显示三态和简短原因；详情页显示完整 diagnostics。相同 package ID/version 但内容变化时提示用户重载/迁移，绝不自动切换项目绑定。

## 缓存

推理缓存使用完整运行指纹：snapshot identity、精确 singer、stage、保留 token、provider/device、请求输入、输出格式/采样率。变化自动产生新 key；旧缓存由 Lite 空闲时 LRU 回收。
