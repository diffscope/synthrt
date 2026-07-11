# SVS 模块 (`srt::svs`)

namespace: `srt::svs` | target: `synthrt::svs` | 头文件: `include/synthrt/SVS/`

---

## 职责

SVS (Singing Voice Synthesis) 模块定义推理的核心抽象：
- `InferenceSpec` — 推理规格（从声库包加载，可创建 Inference 实例）
- `Inference` — 推理任务（继承 ITask，实现 start/stop/result）
- `InferenceCategory` — 推理类别（管理多个 InferenceSpec）
- `SingerSpec` / `SingerCategory` — 歌手规格与类别
- `InferenceContrib` — 推理贡献层（Schema/Configuration/ImportOptions/RuntimeOptions）

---

## 关键 API

### InferenceSpec

```cpp
// include/synthrt/SVS/InferenceContrib.h
class InferenceSpec : public core::ModuleSpec {
public:
    const std::string &className() const;
    core::DisplayText name() const;
    int apiLevel() const;

    const core::JsonObject &manifestSchema() const;
    core::NO<InferenceSchema> schema() const;

    const core::JsonObject &manifestConfiguration() const;
    core::NO<InferenceConfiguration> configuration() const;

    const std::filesystem::path &path() const;

    // 创建 ImportOptions（从 JSON 配置）
    core::Expected<core::NO<InferenceImportOptions>>
        createImportOptions(const core::JsonValue &options) const;

    // 创建 Inference 实例
    core::Expected<core::NO<Inference>>
        createInference(const core::NO<InferenceImportOptions> &importOptions,
                        const core::NO<InferenceRuntimeOptions> &runtimeOptions) const;
};
```

### Inference

```cpp
// include/synthrt/SVS/Inference.h
class Inference : public core::ITask {
public:
    explicit Inference(const InferenceSpec *spec);
    ~Inference();

    const InferenceSpec *spec() const;
    core::Runtime *SU() const;  // 关联的 Runtime
};
```

`Inference` 继承 `ITask`，生命周期：`initialize(args)` → `start(input)` → `result()` → `stop()`。

### InferenceCategory

```cpp
class InferenceCategory : public core::ModuleCategory {
public:
    std::vector<InferenceSpec *> findInferences(const core::ModuleLocator &identifier) const;
    std::vector<InferenceSpec *> inferences() const;
};
```

### SingerImport / SingerSpec / SingerCategory

```cpp
// include/synthrt/SVS/SingerContrib.h
class SingerImport {
public:
    bool isNull() const;
    InferenceSpec *inference() const;          // 引用的 InferenceSpec 指针
    core::JsonValue manifestOptions() const;    // manifest 原始 options JSON
    core::NO<InferenceImportOptions> options() const;  // 解析后的 ImportOptions
};

class SingerSpec : public core::ModuleSpec {
public:
    const std::string &className() const;
    core::DisplayText name() const;
    int apiLevel() const;
    const core::JsonObject &manifestConfiguration() const;
    core::NO<SingerConfiguration> configuration() const;
    const std::vector<SingerImport> &imports() const;  // 引用的 InferenceSpec + options 列表
    const std::filesystem::path &path() const;
};

class SingerCategory : public core::ModuleCategory {
public:
    std::vector<SingerSpec *> findSingers(const core::ModuleLocator &locator) const;
    std::vector<SingerSpec *> singers() const;
};
```

`SingerImport` 是 `SingerSpec` 引用推理插件的桥梁：每个 import 包含一个 `InferenceSpec*` 指针和对应的 `ImportOptions`。`SingerStageResolver` 遍历 `imports()` 并按 `className` 映射到 5 个 stage。

---

## 调用关系

```
Runtime::loadPackage(path)
  └── 解析 desc.json
        ├── InferenceCategory::parseSpec() → InferenceSpec (Initialized → Ready)
        └── SingerCategory::parseSpec() → SingerSpec (imports 引用 InferenceSpec)

SingerStageResolver::resolve(runtime, packageId, singerId, version)
  ├── runtime.moduleCategory("singer")->as<SingerCategory>()->singers()
  │     └── 找到匹配的 SingerSpec
  └── singerSpec->imports() → 按 inferenceId 查找 InferenceSpec
        └── 构建 StageSet (5 个 StageSpec)

ModelSet::load(kind)
  └── stageSpec.spec->createInference(importOptions, runtimeOptions)
        └── 返回 NO<Inference>
              └── inference->initialize(initArgs)
                    └── inference->start(input) → result()
```

---

## 推理插件实现

5 个 DiffSinger 推理插件位于 `plugins/diffsinger/`：

| 插件 | 类名 | StageKind | 说明 |
|---|---|---|---|
| duration | `ai.svs.DurationInference` | Duration | 时长预测 |
| pitch | `ai.svs.PitchInference` | Pitch | 音高预测 |
| variance | `ai.svs.VarianceInference` | Variance | 变异预测 |
| acoustic | `ai.svs.AcousticInference` | Acoustic | 声学模型 |
| vocoder | `ai.svs.VocoderInference` | Vocoder | 声码器 |

每个插件实现 `InferenceSpec::Impl`（parseSpec/loadSpec）和 `Inference::Impl`（initialize/start/stop/result）。

**singer-provider** 插件 (`plugins/diffsinger/singer-provider/`) 实现 `SingerSpec::Impl`，解析声库包的 singer 配置。

---

## 线程安全

- `Inference::start()` 和 `stop()` 由调用方（lite）外层锁保护并发
- `Inference::result()` 使用 `std::shared_lock<std::shared_mutex>` 保护（VocoderInference 已修复 BF-10）
- `Inference::stop()` 必须检查 `impl.session` 非空（AcousticInference/VocoderInference 已修复 BF-08/09）

---

## loadSpec 静默失败诊断（BF-31）

### 问题

`InferenceCategory::loadSpec(Initialized)` 在 `findInterpreterPlugin()` 返回 null 时只 `break`，不返回错误也不记录日志。这导致：

1. `InferenceSpec` 被标记为 Initialized/Ready，但 `impl.interpreter` 为 null
2. `Runtime::loadPackage()` 返回成功
3. `ModelSet::load()` 调用 `createInference()` 时才返回错误："InferenceSpec interpreter is not initialized"
4. `InferDurationTask::runInference()` 捕获错误并 `qCritical`，但推理已静默失败——无合成输出

### 根因

interpreter 插件未找到的常见原因：
- 插件 DLL 加载失败（缺少依赖 DLL，如 ONNX runtime）
- 插件目录不存在或 `plugin.json` 缺失
- `className` 不匹配（声库配置的 `class` 字段与插件 `key()` 不一致）
- `PluginFactory::addPluginPath` 因目录不存在而静默返回

### 修复

在 `InferenceCategory::loadSpec` 的 Initialized 和 Ready 阶段添加 `srtWarning` 诊断日志：

- `loadSpec(Initialized)`: runtime 为 null、plugin 未找到（含 className）、plugin->create() 返回 null
- `loadSpec(Ready)`: interpreter 为 null（含 className）

日志分类：`svs.inference`，可通过 Logger 回调捕获。

### 注意事项

- **不改变控制流**：spec 仍被标记为 Initialized/Ready（保留元数据检查能力），错误在 `createInference()` 时才返回
- **S2P 无关**：当前 `GetPhonemeNameTask` 直接复制 pronunciation 为 phoneme name（TODO R16 待迁移到 `LanguageService::convert()`），S2P 不参与推理管线
- 此修复依赖于 Logger DLL 边界修复（commit a2471b9），否则 synthrt DLL 的日志无法到达宿主应用
