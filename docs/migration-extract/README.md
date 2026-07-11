# 提取器模型迁移方案总览（v2 修正版）

日期: 2026-07-11（v2: 2026-07-11 修正）

定位: 本文档集描述将 ds-editor-lite 的 rmvpe-infer、game-infer 两个音频特征提取模型及音频处理库迁移到 synthrt 的完整方案。音频处理采用 dataset-tools 的 FFmpeg-based 设计。方案经交互式确认和实际代码核对，作为后续实施的契约基准。

---

## 1. 迁移背景

ds-editor-lite 的 `src/libs/` 下有三个独立库：

| 库 | 职责 | 依赖 |
|---|---|---|
| `rmvpe-infer` | 音高提取（f0 + uv），单 ONNX 模型 | synthrt Core/Driver + audio-util |
| `game-infer` | MIDI 音符提取（note/duration/pitch），4 个 ONNX 模型 | synthrt Core/Driver + audio-util + nlohmann/json |
| `audio-util` | 音频解码（mp3/flac/wav via mpg123/FLAC/libsndfile）+ 重采样（soxr）+ RMS 切片 | libsndfile + soxr + mpg123 + FLAC |

**迁移动机：**
- 两个提取器是通用音频分析能力，非 DiffSinger 专属，适合放入 synthrt 作为高级通用功能
- 当前耦合了文件 I/O、音频预处理、ONNX 推理，违反 ARCH-01（职责单一）
- audio-util 使用 soxr+mpg123+FLAC+libsndfile 四个独立依赖，而 dataset-tools 已有更优秀的 FFmpeg-based 设计（接口抽象 + PIMPL + 流式解码 + 零拷贝 view 语义）

---

## 2. 设计决策（交互式确认结果）

| 决策点 | 选择 | 理由 |
|---|---|---|
| 架构归属 | 新建通用 `lib/Extract` 模块（`srt::extract`） | 提取器是补充的高级通用功能，与 DS 非高度相关 |
| 输入接口 | 统一为 WAV 内存流（`AudioBuffer`） | 解耦文件 I/O，接口最干净 |
| 音频处理 | 迁移 dataset-tools 的 FFmpeg-based 设计，独立 `lib/Audio` target | 接口抽象（IAudioDecoder/IAudioResampler）+ PIMPL + 流式 + 零拷贝，优于原 audio-util |
| 重采样责任 | 提取器内部自动重采样 | 调用方简单，传入任意采样率的 AudioBuffer 即可 |
| 扩展机制 | 插件体系（DLL 动态加载） | 运行时可扩展，第三方可开发新提取器 |
| 插件注册 | PluginFactory 按 IID 加载（与 InferenceDriverPlugin 模式一致） | 不需要 ModuleCategory，提取器是按需创建的实例 |
| 多版本支持 | 单插件多版本（VersionedTask 模式） | 类比 G2P 的 VersionedTaskManager |
| 接口设计 | 按提取类型独立设计接口（PitchExtractor / MidiExtractor） | f0 和 MIDI 是完全不同的插件类型，各自复用自己的接口 |
| 生命周期 | lite 负责控制（open/extract/close），无自动卸载 | 类比 ModelSet，显式控制 |

---

## 3. v1 方案修正项

| 错误 | 修正 |
|---|---|
| 使用虚构的 `SRT_PLUGIN_EXPORT` 宏 | 实际宏为 `SRT_EXPORT_PLUGIN(ClassName)`（见 Plugin.h:64） |
| 使用虚构的 `srt_add_plugin()` CMake 函数 | 实际由 BuildAPI.cmake 生成 `srt_extract_add_plugin()`（需 lib/Extract 项目引入 BuildAPI） |
| 假设需要 "extract" ModuleCategory | **不需要**。提取器通过 PluginFactory 按需创建，不存入 ModuleCategory。ModuleCategory 用于共享单例（如 dsdriver），提取器是 per-task 实例 |
| Plugin 用 `extractorType()` 区分类型 | 实际用 IID 模式：`staticIid()` 返回 `"srt.extract.PitchExtractor"` / `"srt.extract.MidiExtractor"`（类比 `kInferenceDriverPluginIid`） |
| `as<T>()` 是 dynamic_cast | 实际是 `std::static_pointer_cast<T>`（NamedObject.h:102），对象必须实际为目标类型 |
| 提取器无法获取 Runtime | `createExtractor(Runtime*)` 传入 Runtime，提取器通过 `runtime->moduleCategory("inference")->getFirstObject("dsdriver")` 获取 ONNX 驱动 |
| audio-util 用 soxr+mpg123+FLAC+libsndfile | 改用 dataset-tools 的 FFmpeg-based 设计（IAudioDecoder + IAudioResampler + PIMPL） |
| 忽略 Game 模型可配置参数 | MidiExtractor 接口需暴露 seg_threshold/est_threshold/language/d3pm_ts 等参数 |
| Slicer 构造参数错误 | rmvpe 传 `Slicer(160, ...)`，game 传 `Slicer(tar_sr, ...)`，第一参数在 rmvpe 中实际为 hopSize 而非 sampleRate |

---

## 4. 文档结构

| 文档 | 内容 |
|---|---|
| [01-audio-module.md](01-audio-module.md) | `lib/Audio` 模块设计：基于 FFmpeg 的 AudioBuffer、IAudioDecoder、IAudioResampler、AudioPipeline |
| [02-extract-module.md](02-extract-module.md) | `lib/Extract` 模块设计：PitchExtractor、MidiExtractor、Plugin 工厂 |
| [03-plugin-architecture.md](03-plugin-architecture.md) | 插件体系：SRT_EXPORT_PLUGIN、IID 模式、VersionedTask 多版本、CMake |
| [04-interface-design.md](04-interface-design.md) | 接口定义：所有公开 API 的完整 C++ 声明 |
| [05-migration-steps.md](05-migration-steps.md) | 分步迁移计划与 lite 适配方案 |

---

## 5. 架构总览

```
synthrt/
  include/
    synthrt/
      Audio/              ← 新增：基于 FFmpeg 的音频处理
        AudioBuffer.h     ← view/owned 语义，多采样格式
        AudioFormatInfo.h
        IAudioDecoder.h   ← 抽象接口（ARCH-02）
        IAudioResampler.h ← 抽象接口
        FfmpegAudioDecoder.h
        SwresampleResampler.h
        AudioPipeline.h   ← 解码+重采样便捷组合
        AudioFileWriter.h
        ResampleConfig.h
        Slicer.h          ← 从 audio-util 迁移（RMS 切片）
  lib/
    Audio/                ← 新增 target: synthrt-audio
    Extract/              ← 新增 target: synthrt-extract
  plugins/
    Extract/              ← 新增插件目录
      rmvpe/              ← 音高提取插件（PitchExtractorPlugin）
      game/               ← MIDI 提取插件（MidiExtractorPlugin）
```

**依赖链：**
```
lib/Core            ← 基础设施（Expected、Error、NamedObject、Plugin）
  ↑
lib/Audio           ← 新增：依赖 Core + FFmpeg（avformat/avcodec/avutil/swresample）
  ↑
lib/Driver          ← ONNX 驱动（已有，不变）
  ↑
lib/Extract         ← 新增：依赖 Core + Driver + Audio
  ↑
plugins/Extract/*   ← 提取器插件 DLL
```

**关键：提取器不需要 ModuleCategory。** PluginFactory 按 IID 加载插件 DLL，lite 调用 `plugin->createExtractor(runtime)` 按需创建提取器实例。

---

## 6. 设计准则核对

| 准则 | 核对结果 |
|---|---|
| ARCH-01 插件职责单一 | 每个提取器插件只负责一种提取类型 ✓ |
| ARCH-02 依赖抽象 | IAudioDecoder/IAudioResampler 抽象接口 ✓ |
| ARCH-03 组合优于继承 | lite 直接组合 Plugin + AudioBuffer ✓ |
| ARCH-05 最小但完整的模型生命周期 | open/extract/close 覆盖完整生命周期 ✓ |
| ROBUST-01 Expected 传播 | 所有 API 返回 `Expected<T>` ✓ |
| ROBUST-02 异常边界隔离 | FFmpeg C API 异常在 PIMPL 边界转换为 Error ✓ |
| INFRA-01 目录表达职责 | lib/Audio、lib/Extract、plugins/Extract/ 各司其职 ✓ |
| INFRA-05 依赖最小化 | 用 FFmpeg 替代 soxr+mpg123+FLAC+libsndfile（1 个依赖替代 4 个） ✓ |
| INFRA-13 PIMPL 隔离 | FFmpeg 头文件不暴露到公开 API ✓ |
| CODING-01 C++20 | `#pragma once`、`m_` 前缀、`srt::audio`/`srt::extract` namespace ✓ |
| CODING-05 开闭原则 | 新增提取器插件不需修改 synthrt 核心代码 ✓ |

---

## 7. lite 调用示例（目标形态）

```cpp
// 1. 初始化 Runtime + ONNX 驱动（现有逻辑不变）
srt::core::Runtime runtime;
srt::driver::setupOnnxInferenceDriver(runtime, pluginRoot, driverCfg);

// 2. 注册提取器插件搜索路径
auto *plugins = runtime.services().get<srt::core::PluginFactory>();
plugins->addPluginPath("srt.extract.PitchExtractor", extractPluginDir);
plugins->addPluginPath("srt.extract.MidiExtractor", extractPluginDir);

// 3. 获取音高提取器插件，创建提取器实例
auto *rmvpePlugin = plugins->plugin<srt::extract::PitchExtractorPlugin>("rmvpe");
auto extractorExp = rmvpePlugin->createExtractor(&runtime);
auto extractor = extractorExp.take();

// 4. 打开模型
extractor->open(modelPath);

// 5. 解码音频文件为 AudioBuffer（FFmpeg-based）
srt::audio::AudioPipeline pipeline = srt::audio::AudioPipeline::create();
auto bufferExp = pipeline.decodeToMonoFloat(filepath, 16000);
// 或任意采样率：pipeline.decodeAndResample(filepath, config)
auto buffer = bufferExp.take();

// 6. 提取（提取器内部自动重采样到 16000Hz mono）
auto resultExp = extractor->extract(buffer, [](int progress) {
    // 更新进度
});
auto result = resultExp.take();  // PitchResult

// 7. 使用结果
for (const auto &frame : result.frames) {
    // frame.offset, frame.f0, frame.uv
}

// 8. 关闭（lite 显式控制生命周期）
extractor->close();
```
