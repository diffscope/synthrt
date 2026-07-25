# DS Infer 模块 (`ds::infer`)

namespace: `ds::infer` | target: `srt-ds::infer` | 头文件: `include/diffsinger/Infer/`

---

## 职责

DS Infer 模块是 DiffSinger 推理编排层：
- `StageKind` — 5 个推理阶段枚举
- `StageSet` — 5 个 StageSpec 的集合
- `SingerStageResolver` — 从 Runtime 解析 singer 的 5 个 stage
- `ModelSet` — 按 stage 惰性加载/复用/卸载 Inference
- `InferenceService` — 全流水线便利封装（CLI/批量）

---

## 关键 API

### StageKind

```cpp
// include/diffsinger/Infer/StageKind.h
enum class StageKind {
    Duration,
    Pitch,
    Variance,
    Acoustic,
    Vocoder,
};
```

### StageSpec 与 StageSet

```cpp
// include/diffsinger/Infer/InferenceService.h
class InferenceService {
public:
    struct StageSpec {
        StageKind kind = StageKind::Duration;
        srt::svs::InferenceSpec *spec = nullptr;
        srt::core::NO<srt::svs::InferenceImportOptions> options;
        std::filesystem::path packageDirectory;  // 实际值为 spec->path()
    };
    // ...
};

// include/diffsinger/Infer/SingerStageResolver.h
struct StageSet {
    InferenceService::StageSpec duration;
    InferenceService::StageSpec pitch;
    InferenceService::StageSpec variance;
    InferenceService::StageSpec acoustic;
    InferenceService::StageSpec vocoder;

    const InferenceService::StageSpec *find(StageKind kind) const noexcept;
};
```

### SingerStageResolver

```cpp
class SingerStageResolver {
public:
    SingerStageResolver() = default;

    // 从 Runtime 解析。收集所有匹配 singerId 的 SingerSpec 候选：
    // - 单个候选：直接使用（忽略 packageId/version，兼容 CLI 单包场景）
    // - 多个候选 + packageId 非空：按 spec->packageId() 精确字符串匹配消歧
    // - 多个候选 + packageId/version 均空：返回 ambiguity 错误（不静默取第一个）
    Expected<StageSet> resolve(
        srt::core::Runtime &runtime,
        const std::string &packageId,
        const std::string &singerId,
        const std::string &version = {});

    // 从已有 SingerSpec 指针直接解析
    Expected<StageSet> resolve(const srt::svs::SingerSpec *singerSpec);
};
```

**注意**: `SingerSpec` 通过 `packageId()`/`packageVersion()` 暴露包标识（由 `Runtime::loadPackage` 从 `desc.json` 注入）。多候选消歧按 `spec->packageId()` 精确字符串匹配（BF-23 v2 修复，不再依赖路径目录名）。`SingerImport` 支持显式跨包声明：import JSON 中声明 `"package"` + `"version"` 时跨包解析（ARCH-06），version 支持 `"*"` 通配或精确匹配；未声明时严格同包隔离。

**多版本歧义错误码 (D-41)**: 当 `(packageId, singerId, version)` 元组匹配多个 `SingerSpec` 时返回 `ErrorCode::SvsSingerAmbiguous`（V3-21，601），不再使用 `SvsSingerNotFound`。镜像 G2P 侧 `G2pVersionAmbiguous` (321)，保持两侧语义对称。错误创建使用 `Error::inferenceError()`，包含 `singerId` 上下文。重复加载使用 `ErrorCode::PackageDuplicate`，stage 缺失使用 `InferenceStageMissing`，未找到使用 `SvsSingerNotFound`。

**D-41 与 D-42 的关系**: D-41 修复 `SingerStageResolver`（stage 层）的歧义错误码语义；D-42 修复 `VoicebankSession::findSinger`（snapshot 层）的静默选择（见 [ds-session.md](ds-session.md#findsinger-多版本歧义拒绝-d-42)）。两者共同构成多版本歧义的两层防护：snapshot 层先拒绝、stage 层兜底。

### ModelSet

```cpp
// include/diffsinger/Infer/ModelSet.h
class ModelSet {
public:
    explicit ModelSet(StageSet stages);
    ~ModelSet();

    ModelSet(const ModelSet &) = delete;
    ModelSet &operator=(const ModelSet &) = delete;
    ModelSet(ModelSet &&) noexcept;
    ModelSet &operator=(ModelSet &&) noexcept;

    // 惰性加载（首次 createInference + initialize，后续复用）
    Expected<srt::svs::Inference *> load(StageKind kind);

    // 获取已加载模型的 NO 引用（未加载返回空 NO）
    srt::core::NO<srt::svs::Inference> &model(StageKind kind) noexcept;
    const srt::core::NO<srt::svs::Inference> &model(StageKind kind) const noexcept;

    // 停止推理但不释放模型
    Expected<void> stop(StageKind kind);

    // 卸载模型（先 stop 再释放）
    Expected<void> unload(StageKind kind);

    // 逆序卸载全部（vocoder → acoustic → variance → pitch → duration）
    Expected<void> unloadAll();

    bool isLoaded(StageKind kind) const noexcept;
    const StageSet &stages() const noexcept;
};
```

**关键**: `load()` 返回 `Expected<Inference *>`（不是引用，因为 Expected 禁止引用类型）。`model()` 返回 `NO<Inference>&`，调用方可拷贝延长引用计数。`stop()` 在模型已处于 Stopped/Failed 状态时静默返回成功（BF-24 修复）。错误创建使用 `Error::inferenceError()` 工厂函数，自动填充 singerId/moduleId 上下文。

### InferenceService

```cpp
class InferenceService {
public:
    // 注入 5 个 stage（call before run()）
    Expected<void> setStages(const StageSpec &duration,
                             const StageSpec &pitch,
                             const StageSpec &variance,
                             const StageSpec &acoustic,
                             const StageSpec &vocoder);

    // 全流水线（duration → pitch → variance → acoustic → vocoder）
    InferenceResult run(const InferenceRequest &request);

    // 单 stage 便利方法（每次创建临时 Inference，不复用）
    Expected<NO<DurationResult>> runDuration(const NO<DurationStartInput> &input);
    Expected<NO<PitchResult>> runPitch(const NO<PitchStartInput> &input);
    Expected<NO<VarianceResult>> runVariance(const NO<VarianceStartInput> &input);
    Expected<NO<AcousticResult>> runAcoustic(const NO<AcousticStartInput> &input);
    Expected<NO<VocoderResult>> runVocoder(const NO<VocoderStartInput> &input);
};
```

`run()` 在各阶段错误传播点调用 `error.appendTrace(std::source_location::current(), "InferenceService::run")`，记录跨层调用链（BF-27）。`InferenceResult.error` 字段类型为 `srt::core::Error`（v4 从 `Diagnostic` 改为 `Error`），包含错误码、分类、消息、源位置和 trace。

---

## 两种调用模式

### 模式 A: ModelSet 惰性加载（lite 风格）

```cpp
// 1. 解析 stage
auto stageSetExp = resolver.resolve(runtime, packageId, singerId, version);
ds::infer::ModelSet modelSet(std::move(*stageSetExp));

// 2. 按需加载 + 推理
auto loadExp = modelSet.load(StageKind::Duration);
auto &durationModel = modelSet.model(StageKind::Duration);
auto resultExp = durationModel->start(input);
auto result = durationModel->result();

// 3. 独立卸载
modelSet.unload(StageKind::Vocoder);  // 释放 vocoder，保留 acoustic

// 4. 全量释放
modelSet.unloadAll();
```

### 模式 B: InferenceService 全流水线（CLI 风格）

```cpp
ds::infer::InferenceService service;
service.setStages(duration, pitch, variance, acoustic, vocoder);
ds::infer::InferenceResult result = service.run(request);
// result.audio 包含 PCM 数据
```

**区别**: ModelSet 模式支持惰性加载和独立卸载（显存优化）；InferenceService 模式每次创建临时 Inference，不复用。Lite 使用 ModelSet 模式，CLI 可使用任一模式。

---

## 调用关系

```
宿主层
  ├── SingerStageResolver::resolve(runtime, packageId, singerId, version)
  │     ├── runtime.moduleCategory("singer")->as<SingerCategory>()->singers()
  │     └── singerSpec->imports() → 按 inferenceId 查找 InferenceSpec
  │           └── 构建 StageSet
  │
  ├── ModelSet(stages)  — 仅存储 StageSet，不创建模型
  │
  ├── modelSet.load(kind)
  │     └── stageSpec.spec->createInference(options, runtimeOptions)
  │           └── inference->initialize(initArgs)
  │
  ├── modelSet.model(kind)->start(input)
  │     └── 返回 TaskResult，通过 NO::as<TypedResult>() 转换
  │
  ├── modelSet.stop(kind)   — 停止但不释放
  ├── modelSet.unload(kind) — 停止并释放
  └── modelSet.unloadAll()  — 逆序释放全部
```

---

## inferutil 共享库

`domains/ds-infer/lib/Util/InferUtil/` 提供 5 个推理插件共享的辅助代码：

| 组件 | 文件 | 说明 |
|---|---|---|
| Algorithm | `Algorithm.h` | arange/interpolate/resample/fillRestMidi |
| TensorHelper | `TensorHelper.h` | 张量创建/写入/溢出检查 |
| Speedup | `Speedup.h` | `getSpeedupFromSteps()` 步长归一化 |
| Driver | `Driver.cpp` | `getInferenceDriver()` 从 Runtime 获取驱动 |
| InputWord | `InputWord.cpp` | 音素预处理（token/language） |
| LinguisticEncoder | `LinguisticEncoder.cpp` | 语言编码器输入构建 |
| SpeakerEmbedding | `SpeakerEmbedding.cpp` | 说话人嵌入 |
| PluginCommon | `PluginCommon.h` | 推理插件共享的模板化参数校验与流程级工具（见下文） |
| InputParser | `InputParser/` | 5 个 stage 的输入解析器 |
| WavFile | `WavFile/` | WAV 文件读写（使用 dr_wav） |

### PluginCommon.h

5 个推理插件（acoustic/duration/pitch/variance/vocoder）的 `.cpp` 中 `getConfig` / `initialize` 前置校验 / `start` 前置校验 / driver 检查 / session 打开 / frameWidth 校验等模板高度一致（CODING-05 >60% 重叠），提取为模板/内联函数。两阶段实施：阶段 1（commit `64d0012`）提取 4 个参数校验函数，阶段 2（commit `c1f46c8`）提取 3 个流程级函数。

**设计约束**：
- D-11：仅提取内部实现，不动 `srt::svs::Inference` 公共类签名
- ARCH-01：不引入新职责，仅工具函数组合；不调用 `setState(Failed)`，状态机由调用方控制
- ARCH-03：组合优于继承，不引入中间基类
- CODING-04：工具函数不含 mutex 加锁，由调用方显式控制（`shared_lock` / `unique_lock`）
- ROBUST-03：所有指针/句柄参数均防空
- ROBUST-05：错误消息保留 logPrefix，不丢失上下文

**API 列表**（namespace `ds::infer::inferutil`）：

| 函数 | 签名 | 错误码 | 用途 |
|---|---|---|---|
| `getTypedConfig<T>` | `(spec, apiClass, apiName, logPrefix) → Expected<NO<T>>` | `InvalidArgument` | 获取并校验 `InferenceSpec::configuration()` 的 typed configuration |
| `getTypedSchema<T>` | `(spec, apiClass, apiName, logPrefix) → Expected<NO<T>>` | `InvalidArgument` | 获取并校验 `InferenceSpec::schema()` 的 typed schema（仅 VarianceInference 使用） |
| `validateInitArgs` | `(args, apiName, logPrefix) → Expected<void>` | `InvalidArgument` | 校验 `Inference::initialize()` 的 args 非空且 objectName 匹配 |
| `validateStartInput` | `(input, apiName, logPrefix) → Expected<void>` | `InvalidArgument` | 校验 `Inference::start()` 的 input 非空且 objectName 匹配 |
| `checkDriverReady` | `(driver, logPrefix) → Expected<void>` | `InferenceStartFailed` | 校验 `InferenceDriver` 已初始化（用于 `start()` 开头） |
| `openOnnxSession` | `(driver, modelPath, useCpu, sessionName, logPrefix) → Expected<NO<InferenceSession>>` | `InvalidArgument` / `InferenceStartFailed` | 创建并打开 ONNX session（用于 `initialize()` 中 encoder/predictor session） |
| `validateFrameWidth` | `(frameWidth, logPrefix) → Expected<void>` | `InvalidArgument` | 校验 frameWidth 为正有限数（`std::isfinite && > 0`） |

**错误码差异**：`checkDriverReady` 返回 `InferenceStartFailed`（运行时状态错误，driver 应已初始化但未初始化），其余空指针路径返回 `InvalidArgument`（参数错误）。这反映了 lite 调用模式差异——lite 调用 `start()` 时若 driver 为空属于运行时状态错误，而非参数错误。

**lite 调用模式**：lite 只调 `inference->start(input)`，不调 `initialize()`（由 `ModelSetHandle::load()` 内部触发）。`start()` 开头的 `checkDriverReady` 是 stale session 的主要防护边界（`SingerModelSession.cpp:42-43` 注释："Lite calls Inference::start() directly so this acquire-time check is the primary staleness boundary."）。

**测试覆盖**：`test_plugin_common_extreme.cpp`（commit `dd535cf`）覆盖 7 个函数的极端情况（BF-43 ~ BF-49，27 个测试用例 / 84 个断言），包括 NaN/Inf/0/负 frameWidth、null driver/args/input/spec、session open 失败（FailOpenDriver/FailOpenSession mock）、name 不匹配、logPrefix 保留一致性。`getTypedConfig` / `getTypedSchema` 的 `configuration=nullptr` / `class/name 不匹配` 路径需 `InferenceSpec` 实例（构造函数 protected，且 `configuration()` 非 virtual），由 5 个插件 `.cpp` 集成测试覆盖。

Catch2 单元测试位于 `domains/ds-infer/unittests/catch2/`，覆盖 Algorithm/TensorHelper/VersionUtils 以及 v4 新增的 `test_modelset_errors.cpp`（错误路径 + BF-24 回归）、`test_singer_resolver_ambiguity.cpp`（BF-23 回归）、`test_speaker_mapper.cpp`（BF-22 + BF-31 跨包隔离回归）、`test_model_registry.cpp`（BF-31 跨包同 id 推理隔离）、`test_package_isolation.cpp`（BF-29/BF-30 多版本隔离与跨包声明）、`test_plugin_common_extreme.cpp`（BF-43 ~ BF-49，PluginCommon.h 7 个工具函数的极端情况，含 FailOpenDriver/FailOpenSession mock）。`test_version_utils.cpp` / `test_version_utils_complex.cpp` 在原有 normalizeVersion/compareVersions/VersionRange/VersionResolver 基础用例之上，新增 `[extreme]` 极端用例（仅 v/V 前缀、多段 pre-release、首尾点、全非数字段、空串/溢出段比较、COMPATIBLE 单段、反向 hyphen 区间、parseError 校验、selectHighestVersion 空/单元素、重复版本去重、level=-1 回退请求方 level、0.0.0 可选）与 BF-32 多约束回归。

---

## 已修复的关键 Bug

| Bug | 说明 |
|---|---|
| BF-01 | SingerStageResolver 现在匹配 packageId + version |
| BF-05 | ModelSet::slot() 无效 StageKind 时 abort 而非静默回退 |
| BF-06 | InferenceService::run vocoder 音频拷贝不再越界 |
| BF-13 | getInferenceDriver 现在检查 nullptr |
| BF-15 | AcousticInference 除零保护（steps=0） |
| BF-17 | ModelSet::unloadAll 继续卸载剩余阶段（首次失败不中断） |
| BF-22 | SpeakerMapper::resolve() 返回 Expected，不再静默返回空字符串 |
| BF-23 | SingerStageResolver 按 packageId 字符串精确匹配，不再依赖路径目录名 |
| BF-24 | ModelSet::stop() 检查 state() != Running，已停止模型不报错 |
| BF-27 | InferenceService::run() 在 11 个错误传播点追加 appendTrace |
| BF-28 | AcousticInference speedup 钳制最小值 1，防止除零 |
| BF-29 | Runtime::loadPackage 检测重复 spec 加载（id+packageId+version 严格匹配） |
| BF-30 | SingerImport 支持显式跨包声明（package+version，ARCH-06 跨包 stage 共享） |
| BF-31 | ModelRegistry/SpeakerMapper 按 (packageId, inferenceId) 复合键隔离，避免跨包同 id 推理冲突；InferenceInfo 新增 packageId 字段由 PackageParser 注入 |
| BF-32 | VersionRange 构造器在单算子分支之前增加空白分隔多约束解析：`">=1.0.0 <2.0.0"` 之前被首算子 `>=` 吞掉整个剩余串作为单个版本，上界 `<2.0.0` 被静默丢弃（3.0.0 错误命中）；现按空白拆分为多约束求交集 |
| BF-34 | `preprocessSpeakerEmbeddingFrames` 在 resample 前校验 proportions 非空、多比例时 interval 非 0；之前 `proportions={}` 或 `interval=0 && size>1` 导致 resample 返回空向量，speaker 被静默跳过（ROBUST-05 违规） |
| BF-35 | `AcousticInference::start` 增加 frameWidth 正值校验（`std::isfinite && > 0`），与 Duration/Pitch/Variance 对齐；之前 `hopSize=0` 或 `sampleRate=0` 导致 frameWidth=0/NaN，引发 `preprocessPhonemeDurations` 除零和 resample 静默跳过 |
| BF-36 | `VarianceInference` 输出 dataType 校验对齐 Duration/Pitch：之前仅检查 `view.empty() && elementCount > 0`（只捕获类型不匹配），空输出（elementCount==0）被静默跳过，生成空 values 的 prediction；现先检查 `dataType != Float` 再检查 `view.empty()`，与 Duration/Pitch 完全一致 |
| BF-37 | `DurationInference` speaker embedding 查找静默跳过修复（ROBUST-05）：之前 `config->speakers.find(speaker.name)` 找不到时无 else 分支，该 phoneme 的 embedding 全为 0（静默错误吞没）；现返回 `InferenceSpeakerNotFound` 错误。同时增加 `InputPhonemeInfo::Speaker.embedding` 内联向量支持 |
| BF-38 | `proxy_map::iterator_base` 的 `std::optional<_Ref>` 在 MSVC Debug 模式崩溃（`_ITERATOR_DEBUG_LEVEL != 0` 时 `operator->()` 触发 `_STL_VERIFY`）。`_Ref` 含引用成员（`const std::string &first`, `_Ty &second`），导致 optional 状态不可靠。修复：替换为手动对齐存储（`alignas(_Ref) char[]` + `bool _ref_valid`）+ placement new 构造，拷贝/移动不传递缓存（按需重建）。影响所有调用 `JsonValue::toJson()` 的位置（Core/G2P/ds-bank/dsinfer-cli） |
| BF-39 | OnnxDriver 插件 `qm_add_copy_command` 安装逻辑在 vcpkg Debug 构建下断裂：`_rel_path` 相对 `QMSETUP_BUILD_DIR` 计算，当 vcpkg 覆盖 `CMAKE_LIBRARY_OUTPUT_DIRECTORY`（输出在 `${CMAKE_BINARY_DIR}/lib` 而非 `${QMSETUP_BUILD_DIR}/lib`）时，`_rel_path` 以 `../` 开头，解析到错误的安装目录（Debug runtime 落入 Release 树）。修复：`qm_add_copy_command` 改用 `SKIP_INSTALL`（仅构建阶段复制），安装阶段改用 `install(FILES ...)` + `file(GLOB ...)` 解析实际路径 + 相对 `DESTINATION` 正确解析到安装时 `CMAKE_INSTALL_PREFIX`。跨平台覆盖 Windows(dll)/macOS(dylib)/Linux(so) |
| BF-40 | `OnnxTensor::createFromRawView` 在 `std::memcpy` 前未校验 `data.size() == tensor->_bytesSize`：`data.size() > _bytesSize` 导致堆缓冲区溢出（OOB 写），`data.size() < _bytesSize` 导致张量缓冲区部分未初始化。修复：在拷贝前增加显式大小匹配校验，不匹配时返回 `InvalidArgument` 错误（包含两边字节数） |
| BF-41 | `preprocessPhonemeDurations`（InputWord.cpp）和 `preprocessLinguisticWord`（LinguisticEncoder.cpp）除以 `frameWidth` 时无本地校验。四个推理插件（Duration/Pitch/Variance/Acoustic）在调用前已校验 `frameWidth > 0`，但工具函数本身无防御性检查（defense-in-depth）。修复：在两个函数入口增加 `std::isfinite && > 0` 校验，返回 `InvalidArgument`，与插件侧模式一致 |
| BF-42 | `resample()`/`interpolate()`（Algorithm.h）三处崩溃与静默错误：(1) `interpolate()` 在 `referencePoints.front()/back()` 前未校验非空，空引用数组触发 UB；(2) `resample()` timestep 校验为 `== 0` 漏掉负值，负 timestep 使 `arange()` 返回空 → `interpolate()` 返回空 → `targetSamples.back()` 在空 vector 上崩溃；(3) `preprocessSpeakerEmbeddingFrames` 未校验 `frameWidth > 0`，且 BF-34 的 `interval == 0` 校验漏掉负值，导致 `resample()` 返回空后 speaker 被静默跳过（ROBUST-05）。修复：`interpolate()` 增加空引用数组早返回；`resample()` timestep 校验改为 `<= 0`，并在 `.back()` 前增加 `!empty()` 防御；`preprocessSpeakerEmbeddingFrames` 入口校验 `frameWidth > 0`，BF-34 interval 校验改为 `<= 0` |
| BF-43 | `validateFrameWidth`（PluginCommon.h）极端值测试覆盖：NaN / +Inf / -Inf / 0 / 负数 / subnormal 正数 / 正常正数。验证 `!std::isfinite \|\| <= 0` 条件分支完整。错误码 `InvalidArgument`，错误消息保留 logPrefix（ROBUST-05）。测试文件 `test_plugin_common_extreme.cpp` |
| BF-44 | `checkDriverReady`（PluginCommon.h）空 driver 测试覆盖：默认构造 `NO<InferenceDriver>` 等价于 nullptr。错误码 `InferenceStartFailed`（非 `InvalidArgument`，因为 driver 未初始化属于运行时状态错误，而非参数错误）。错误消息保留 logPrefix |
| BF-45 | `validateInitArgs`（PluginCommon.h）空 args / name 不匹配测试覆盖：错误消息包含 `expected`/`got` 对比，便于排查。验证空 apiName 与非空 args name 不匹配返回错误 |
| BF-46 | `validateStartInput`（PluginCommon.h）空 input / name 不匹配测试覆盖：同 BF-45，但错误消息包含 `start:` 前缀以与 `validateInitArgs` 区分 |
| BF-47 | `getTypedConfig`（PluginCommon.h）空 spec 测试覆盖：ROBUST-03 防空。`configuration=nullptr` 与 `class/name 不匹配` 路径需 InferenceSpec 实例（构造函数 protected，且 `configuration()` 非 virtual），由 5 个插件 .cpp 集成测试覆盖 |
| BF-48 | `getTypedSchema`（PluginCommon.h）空 spec 测试覆盖：同 BF-47，调用 `spec->schema()` |
| BF-49 | `openOnnxSession`（PluginCommon.h）空 driver / session open 失败测试覆盖：使用 `FailOpenDriver`/`FailOpenSession` mock 验证错误传播。错误消息包含 logPrefix / sessionName（"encoder"/"predictor"）/ modelPath（ROBUST-05 不丢失上下文）。验证 useCpu 标志不影响错误路径 |
| D-46 | `Algorithm.h::arange()` 对 NaN/inf 输入无防护——`static_cast<size_t>(std::ceil(NaN))` 是 UB（MSVC x64 上通常为 0 或 SIZE_MAX），`ceil(inf)` 产生 inf 同样 UB；subnormal step 使 `(stop-start)/step` 极大，`reserve()` 抛 `bad_alloc`。`resample()` 的 `timestep <= 0` 检查无法拦截 NaN（`NaN <= 0` 为 false），inf 会触发 `arange(0, inf, targetTimestep)` 的巨大分配。修复：`arange()` 添加 `!std::isfinite` 检查 + 100M 大小上限；`resample()` 添加 `!std::isfinite(timestep/targetTimestep)` 前置检查。同时修正 9 个测试错误期望（arange 负步长对称性、fillRestMidi 紧邻值填充、preprocessPhonemeDurations 帧分配、DynamicMix 线性插值、DictionaryS2P 空格容忍）以对齐实现实际行为 |

## Speaker Embedding Inline API

`InputSpeakerInfo` 和 `InputPhonemeInfo::Speaker` 新增可选的 `embedding` 字段（`std::vector<float>`），允许直接传入混合后的 embedding 向量，而不依赖声库预定义的 speaker name 查找。

### 使用场景

ds-editor-lite 的自定义音色混合机制允许：
- 声库里未定义的 spk（自定义 speaker）
- 混合后的 emb 数值（加权平均后的 embedding 向量）

这些情况是合法的，不应被拦截。Inline embedding 字段让调用方可以绕过声库查找，直接提供 embedding 向量。

### 查找优先级

1. 先在声库的 `speakers` 映射中按 `name` 查找
2. 找不到时，检查 `embedding` 字段是否非空
3. 非空则直接使用传入的 embedding 向量（校验长度 == hiddenSize）
4. 都不可用则返回错误

### 两种混合模式

| 模式 | 数据结构 | shape | 适用推理 |
|------|---------|-------|---------|
| Phoneme 级别 | `InputPhonemeInfo::Speaker{name, proportion, embedding}` | `{1, phoneCount, hiddenSize}` | Duration |
| 帧级别 | `InputSpeakerInfo{name, interval, proportions, embedding}` | `{1, targetLength, hiddenSize}` | Acoustic/Pitch/Variance |

Duration 每个 phoneme 有独立的 speaker 列表和比例；Acoustic/Pitch/Variance 使用顶层 speaker 列表，proportions 是时间序列。
