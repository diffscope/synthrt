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
    // - 多个候选 + packageId/version 非空：通过 spec->path() 包含 packageId 做尽力消歧
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

**注意**: `SingerSpec` 不直接暴露 packageId/version，多候选消歧通过 `spec->path()` 的路径目录名组件逐一匹配 packageId（BF-23 修复，不再用子串搜索避免误匹配），version 仅用于决定是否需要消歧。错误码使用 `ErrorCode::SvsSingerNotFound`/`SvsStageResolveFailed`。

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
| InputParser | `InputParser/` | 5 个 stage 的输入解析器 |
| WavFile | `WavFile/` | WAV 文件读写（使用 dr_wav） |

Catch2 单元测试位于 `domains/ds-infer/unittests/catch2/`，覆盖 Algorithm/TensorHelper/VersionUtils 以及 v4 新增的 `test_modelset_errors.cpp`（错误路径 + BF-24 回归）、`test_singer_resolver_ambiguity.cpp`（BF-23 回归）、`test_speaker_mapper.cpp`（BF-22 回归）。

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
| BF-23 | SingerStageResolver 使用路径目录名组件匹配，不再用子串搜索 |
| BF-24 | ModelSet::stop() 检查 state() != Running，已停止模型不报错 |
| BF-27 | InferenceService::run() 在 11 个错误传播点追加 appendTrace |
| BF-28 | AcousticInference speedup 钳制最小值 1，防止除零 |
