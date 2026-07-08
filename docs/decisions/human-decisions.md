# SynthRT 重要人工决策记录

日期: 2026-07-10

定位: 本文档记录 synthrt 项目在 v2/v3 重构过程中确认的关键人工决策。每条决策均经用户确认，作为后续开发的约束基准。制定新方案时须对照本文档确保不违背已有决策。

---

## 一、v2 核心架构决策（2026-07-09 确认）

### D-01：ModelSet 保留并重新定位

**决策**: ModelSet 保留，动机=惰性按 stage 加载 + 独立卸载（替换 SynthrtEngine 的 5 个 `NO<Inference>` 成员）。

**否决**: 不以"修复模型重复创建"为动机——lite 已通过 SynthrtEngine 实现缓存复用。

**影响**: `ModelSet` 替换 SynthrtEngine 中 eager 加载全部 5 模型的 5 个 `NO<Inference>` 成员，支持 `load(kind)` 按需加载、`unload(kind)` 独立释放（如卸载 vocoder 保留 acoustic）。

### D-02：C ABI Phase 1 迁移 impl，签名不变

**决策**: Phase 1 迁移 `lib/C/srt_v4.cpp` 的 impl 到内部组合 `VoicebankScanner` + `LanguageService` + `Runtime`，C ABI 签名（`srt.h`）保持不变。

**理由**: 解决 v2 §2.6"不做详设"与 Phase 1 删除 ds-session 的矛盾。完整 C ABI ADR（Level 提升、新增函数等）不在 v2 范围。

### D-03：SVS 独立 target

**决策**: 采纳 v2 独立提议，`lib/Core/SVS/` → `lib/SVS/` 独立 target（`synthrt-svs` + `synthrt::svs`）。

**否决**: 反转 v1 Phase 2 方案 A（SVS 留在 Core 内）。

### D-04：Lite §3 重写

**决策**: 重写 lite 对接文档，描述 SynthrtEngine 已存在 + v2 增量（ModelSet 替换 5 个 `NO<Inference>` + 惰性加载）。

### D-05：删除 Session 和 PackageRef 残余

**决策**: `ds-session` 从构建图彻底删除，`include/diffsinger/Session/*` 全部删除（无 forwarding header），`PackageRef` stub 删除。`Runtime::open()` 替换为 `Runtime::loadPackage(path) -> Expected<void>`。

### D-06：LanguageService 迁移归属

**决策**: `LanguageService` 从 `include/diffsinger/Lang/` 移入 `include/synthrt/G2P/LanguageService.h`，归属 `srt::g2p` 模块（export macro 改为 `SRT_G2P_EXPORT`）。namespace 保持 `ds::lang` 不变。

### D-07：不搬入 synthrt 的内容

**决策**: 以下内容保留在 lite 侧，不搬入 synthrt：
- `SynthrtEngine` facade
- `GenericInferModel` JSON 缓存逻辑
- Qt `Task` 状态机
- `InferenceApplyGate`
- WAV 文件写入
- AppOptions 和 GPU 选择 UI
- `InferenceLoader` 清理

---

## 二、v2 落地确认（2026-07-09 ~ 2026-07-10）

### D-08：v2 六阶段全部完成

| Phase | 内容 | 提交 |
|---|---|---|
| P1 | 删除 ds-session + 迁移 LanguageService 到 G2P | ef99fbe |
| P2 | 删除 PackageRef + 新增 Runtime::loadPackage | 808887c |
| P3+4 | ModelSet API + SingerStageResolver 加 version | b046a2b |
| P5 | SVS 独立 target + 目录收敛 | 01b5ae9 |
| P6 | dsinfer-cli lite 风格测试 | 7064cf9 |

### D-09：ModelSet::load 返回类型修正

**决策**: `Expected<Inference &>` 非法（Expected 禁止引用类型），改为 `Expected<Inference *>`。

### D-10：TaskResult::error 与 InferenceResult::error 类型不同

**确认**: `TaskResult::error` 类型为 `srt::core::Error`（有 `code()` 方法），而 `InferenceResult::error` 类型为 `srt::core::Diagnostic`（有 `code` 字段）。typed result 错误检查必须用 `error.code()` 方法调用。

---

## 三、v3 Bug 修复决策（2026-07-10）

### D-11：v3 不改动 v2 已定稿接口

**决策**: v3 聚焦修复内部逻辑 Bug 和隐式错误处理，不改动 v2 已确定的公共接口签名和目录结构。修复方式在不改变公共签名的前提下进行：
- 内部实现可改（`_impl`、private 方法、`.cpp` 内逻辑）
- 返回类型若 v2 已定义为 `Expected<T>`，补充错误路径即可
- 返回类型若 v2 已定义为 `bool`/`void` 且无法改为 `Expected`，通过 `Error*` out-param 或 `Diagnostic` 字段传递错误

### D-12：v3 六阶段全部完成

| Phase | 内容 | 提交 |
|---|---|---|
| P1 | P0 逻辑 Bug（SingerStageResolver + VoicebankScanner + ModelSet） | 3c7c66d |
| P2 | Runtime::loadPackage 显式报错 | - |
| P3 | VersionUtils 修复 | - |
| P4 | LanguageService 显式报错 | - |
| P5 | ONNX Session 异常边界加固 | - |
| P6 | C ABI + InferenceService 加固 | 2f352b2 |

### D-13：BF-08~12 推理插件 Bug 修复

**确认**: 5 个真实 Bug 全部在 diffsinger 推理插件，均为与兄弟插件（Duration/Pitch/Variance）行为不一致的遗漏：
- BF-08: AcousticInference::stop() 空指针解引用
- BF-09: VocoderInference::stop() 空指针解引用
- BF-10: VocoderInference::result() 缺少互斥锁
- BF-11: VocoderInference::initialize() 缺少 setState(Idle)
- BF-12: VocoderInference 错误消息 copy-paste 错误

提交: dcb177e、7f81524

### D-14：BF-13~17 深度扫描修复

**确认**: 5 个真实 Bug 分布在 inferutil 共享库和推理插件：
- BF-13: getInferenceDriver 空指针解引用
- BF-14: preprocessPhonemeLanguages 错误消息用错变量
- BF-15: AcousticInference 除零错误（steps=0）
- BF-16: arange 除零（防御性）
- BF-17: ModelSet::unloadAll 首次失败跳过剩余阶段

提交: 80ec0b5、8b3f735、eccd7dd、15b1740、81093ac

### D-14b：BF-18~20 第四轮扫描修复

**确认**: 3 个真实 Bug 均为推理插件间行为不一致的遗漏：
- BF-18: PitchInference::start() fillRestMidi 失败时缺少 setState(Failed)
- BF-19: VarianceInference::start() 缺少输出 dataType 校验
- BF-20: DurationInference::start() Tensor::create 失败时缺少 setState(Failed)

提交: a0d9641、54be086、4e13fb7

---

## 四、工程约束决策

### D-15：G2P 加载时机

**决策**: Custom G2P 必须只在应用启动时加载，运行时加载禁止，需要重启。Official G2P 版本固定匹配编辑器版本；Custom G2P 绑定声库，不能独立安装。

**历史**: `enableVoicebankG2p` 配置选项已于 2026-07-01 删除，始终注册 voicebank G2P。

### D-16：LangNote 字段简化

**决策**: `LangNote.g2pContext/g2pContextVersion/g2pSource` 字段已于 2026-07-01 删除，5-level route 简化为 4-level。`G2pResult` 保留 g2pContext/g2pContextVersion/g2pSource 用于 UI 显示（输出结构，非输入）。

### D-17：跨项目常量

**决策**: 跨项目常量必须使用而非字符串字面量：
- `LangCore::kG2pSourceVoicebank`
- `LangCore::kG2pModeCopy`
- `LangCore::kG2pModeSkip`

### D-18：VersionUtils 边界处理

**决策**: VersionUtils 必须处理 5+ 段版本号不栈溢出。空 `QVersionNumber` 必须映射到 `stdc::VersionNumber()` 而非 0.0.0。

### D-19：SingerInfo 契约

**决策**: `SingerInfo` 是 PackageManager 和 Language 模块之间的唯一信息契约，包含显式 `resolutionState` 字段（Resolved/Pending/Missing，默认 Pending）。Clip-to-singerInfo 设计使用快照语义和 per-clip 隔离。

### D-20：ONNX 驱动全局基础设施

**决策**: ONNX 驱动是全局基础设施，命名为 `g2pOnnxDriver`，必须在 `langMgr->initialize()` 之前注册。

### D-21：禁止的危险模式

**确认**: 以下模式已确认会导致问题，禁止使用：
- `qFatal()` 在后台任务中导致意外应用终止
- `Q_ASSERT()` 在推理任务非关键校验中（Debug 生效 Release 消失）
- `ParamTag` 使用 `const std::string_view` 成员导致 copy assignment 被隐式删除，破坏 `std::vector` 操作
- 共享数据类的 copy constructor 必须正确初始化所有字段（如 `resolutionState`）

### D-22：依赖清理

**决策**: 2026-07-10 删除 boost-test 依赖。`domains/ds-infer/unittests/auto/` 仅有空 boost-test main stub（无实际测试用例），全部 ds-infer 测试使用 Catch2 v3。保证功能不变。

---

## 五、构建与发布

### D-23：构建方式

**确认**: 无 CMakePresets.json。使用 `build-release.bat`（设置 MSVC 环境，运行 `cmake --build cmake-build-release --config Release`，Ninja 生成器，MSVC 14.51.36231）。

### D-24：提交规范

**确认**: 每个任务单独提交，不推送。提交信息格式：`<type>(<scope>): <简述>`。完成单个任务后在文档更新状态标记。

### D-25：v1 文档废弃

**决策**: v1 文档（`docs/refactoring-v1/`，7 文件）已于 2026-07-09 删除，v2 取代。v1 Phase 1+2 已落地成果保留（SVS namespace、Contribute.h 删除等），v2 文档不再重复记录。
