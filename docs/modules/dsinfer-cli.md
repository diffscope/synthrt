# dsinfer-cli 模块 (`dsinfer_cli`)

namespace: `dsinfer_cli` | target: `dsinfer-cli` | 源文件: `tools/dsinfer-cli/`

---

## 职责

dsinfer-cli 是 DiffSinger 推理流水线的命令行前端，承担：
- 命令行参数解析（`CliArgs`）：mode / packageDir / inputPath / speakerId / outputDir /
  languageId / ep / deviceIndex + 命名参数（`--g2p-packages` / `--plugin-paths` /
  `--plugin-root` / `--dump-data` / `--test-lite-style`）
- 输入解析（`midi_parser` / `dspx_parser`）：将 MIDI / DSPX 文件解析为
  `dsinfer_cli::Note` 列表
- 段落构建（`segment_builder`）：将 Note 列表按静音切分为 InferenceRequest 段
- G2P/S2P 流水线（`g2p_s2p_pipeline`）：将歌词转为音素序列与 onset 标记
- 推理调度（`main.cpp`）：通过 `ds::infer::InferenceService` 或 `ModelSet`
  （`--test-lite-style`）执行 5 阶段推理（Duration / Pitch / Variance / Acoustic / Vocoder）
- 输出（`wav_writer`）：将 `InferenceResult.audio` 写为 16-bit PCM WAV

---

## 与 lib/ / domains/ 的接口契约

### 1. 包加载（`lib/Core` + `domains/ds-bank`）

| 阶段 | CLI 调用方 | lib/ / domains/ API | 错误约定 |
|------|------------|----------------------|---------|
| Runtime 初始化 | `main.cpp::initializeSU()` | `synthrt::core::Runtime::system()` / `Runtime::setPluginPaths()` | 返回 `Expected<>`，失败向上传播 |
| 声库扫描 | `main.cpp::initializeSU()` | `ds::bank::VoicebankScanner::refresh()` | `Expected<std::vector<PackageStatus>>`，`.error()` 含路径细节 |
| 包加载 | `main.cpp::loadPackage()` | `synthrt::core::Runtime::loadPackage(packageDir)` | `Expected<NO<Package>>`，包不存在或 manifest 缺失时失败 |
| 推理服务初始化 | `main.cpp::initInferenceService()` | `ds::infer::InferenceService::setStages()` / `setSinger()` | `Expected<void>`，stage spec 为 null 时 `InvalidArgument` |

**契约**：`CliArgs.packageDir` 必须是包含 `desc.json` 的合法 DiffSinger 包目录；
若不存在则 `Runtime::loadPackage` 返回错误，CLI 退出码 1。

### 2. G2P / S2P（`lib/G2P` + `lib/S2P`）

| 阶段 | CLI 调用方 | lib/ API | 错误约定 |
|------|------------|----------|---------|
| 语言路由解析 | `g2p_s2p_pipeline::resolveRoute()` | `srt::g2p::LanguageService::resolveLanguageRoute(g2pId, context)` | `Expected<LanguageRoute>`；BUG-CLI-015 修复后错误显式上报 |
| 歌词转换 | `g2p_s2p_pipeline::convertLyric()` | `LanguageRoute::convertLyric(lyric)` | 返回 `LyricConvertResult`，`mode == kG2pModeCopy` 表示 copy fallback |
| 音素序列化 | `g2p_s2p_pipeline::convertToS2p()` | `srt::s2p::LanguageResource::direct()` / `dictionary()` | 构造时若资源缺失抛异常，CLI catch 后退出码 1 |
| 音素→音素序列 | `g2p_s2p_pipeline::convertToS2p()` | `LanguageResource::convert(pronunciation)` | 返回 `SyllablePronunciation{phonemes, onsets}` |

**契约**：
- `--g2p-packages` 参数覆盖默认 `RuntimeLayout::g2pPackagesRoot(app_dir)` 路径
- `--plugin-paths` 覆盖默认 `[pluginRoot/srt-g2p/G2ps, pluginRoot/srt-g2p/dict]`
- copy fallback 触发 `cliLog.srtWarning`（见 `cli_log.h` 的 P0-3 级别约定）

### 3. 推理执行（`domains/ds-infer`）

CLI 提供两条推理路径：

#### 3a. 默认路径：`InferenceService::run`（推荐）

```cpp
// main.cpp::execPipeline()
auto result = inferenceService->run(request);
if (!result) { /* result.error() */ }
```

**契约**：`InferenceService::run` 内部按序调度 5 阶段，返回 `InferenceResult`。
CLI 仅消费 `result.audio` / `result.sampleRate`，不感知中间阶段。

#### 3b. Lite 风格路径：`runLiteStylePipeline`（`--test-lite-style`）

```cpp
// main.cpp::runLiteStylePipeline() (tools/dsinfer-cli/main.cpp:57-...)
auto result = runLiteStylePipeline(stages, request);
```

**契约**：手动调用 `ModelSet::load / start / stop / unload`，逐阶段执行，
与 `InferenceService::run` 产出相同的 `InferenceResult`。用于在 CLI 中复现
`ds-editor-lite` 的 lazy load + lifecycle 模式（参见 TD-CLI-12 评估）。

### 4. 输出（`wav_writer`）

| 阶段 | CLI 调用方 | lib/ API | 错误约定 |
|------|------------|----------|---------|
| 写 WAV | `wav_writer::writeWav()` | 消费 `InferenceResult.audio` (float*) + `sampleRate` | `bool` 返回，失败时 `cliLog.srtCritical` |

**契约**：`InferenceResult.audio` 为交错 float PCM，`sampleRate` 来自
vocoder stage 输出。`wav_writer` 转为 16-bit signed PCM，写入
`outputDir / <input_stem>.wav`。

---

## 调用顺序

```
main()
├── CliArgs::parse()                            // 解析参数
├── initializeSU()                              // 加载 Runtime + 声库扫描
│   ├── Runtime::setPluginPaths(pluginPaths)
│   ├── VoicebankScanner::refresh()
│   └── Runtime::loadPackage(packageDir)
├── installLogCallback()                        // TD-CLI-04: 幂等
├── initInferenceService()
│   ├── InferenceService::setStages(stages)
│   └── InferenceService::setSinger(speakerId)
├── parseMidi() / parseDspx()                   // 解析输入
├── segment_builder::buildSegments()            // 切段
├── g2p_s2p_pipeline::buildWords()              // G2P + S2P
├── execPipeline()                              // 推理
│   └── InferenceService::run(request) [默认]
│       或 runLiteStylePipeline(stages, request) [--test-lite-style]
└── wav_writer::writeWav(result.audio)         // 输出
```

---

## 错误处理约定

| 来源 | 类型 | CLI 处理 |
|------|------|---------|
| `Runtime::loadPackage` | `Expected<NO<Package>>` | `.error().message()` → stderr，退出码 1 |
| `VoicebankScanner::refresh` | `Expected<std::vector<PackageStatus>>` | 同上 |
| `LanguageService::resolveLanguageRoute` | `Expected<LanguageRoute>` | 同上（BUG-CLI-015 修复后显式上报） |
| `InferenceService::run` | `InferenceResult.error` | `result.error.message` → stderr，退出码 1 |
| `parseMidi` / `parseDspx` | `bool` / `Expected<>` | 失败时 stderr + 退出码 1 |
| `wav_writer::writeWav` | `bool` | 失败时 `cliLog.srtCritical` + 退出码 1 |
| 框架日志回调异常 | 异常穿越回调边界（UB） | `log_report_callback` 内部 try-catch（ROBUST-02） |

---

## 测试覆盖

测试基础设施（TD-CLI-01）位于 `unittests/tools/tst_dsinfer_cli.cpp`，
通过 subprocess 启动 `dsinfer-cli.exe` 进行 L2 黑盒测试，覆盖：
- `--version` / `-h` / `--help` 退出码与输出
- 无参数 / 参数不足 / 未知 mode / 无效 ep / 无效 device index 退出码
- `--g2p-packages` / `--plugin-paths` 无值退出码
- BUG-CLI-006 回归保护（max_segments 参数已删除）

详细测试项见 [docs/audit/uncovered-test-items.md](../audit/uncovered-test-items.md)
的 GAP-NEW-001 / GAP-NEW-005。

---

## 相关文档

- [docs/design/design-guidelines.md](../design/design-guidelines.md) — ARCH / ROBUST / INFRA / CODING 准则
- [docs/decisions/human-decisions.md](../decisions/human-decisions.md) — D-11 / D-24 / D-47 等
- [docs/audit/hidden-bugs-followup.md](../audit/hidden-bugs-followup.md) — BUG-CLI-* 清单
- [docs/audit/technical-debt-remaining.md](../audit/technical-debt-remaining.md) — TD-CLI-* 技术债
- [docs/modules/ds-infer.md](ds-infer.md) — InferenceService / ModelSet API
- [docs/modules/g2p.md](g2p.md) — LanguageService API
- [docs/modules/s2p.md](s2p.md) — LanguageResource API
