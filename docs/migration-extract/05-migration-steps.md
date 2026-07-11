# 05 - 分步迁移计划（v2 修正）

日期: 2026-07-11（v2: 2026-07-11 修正）

定位: 定义从 ds-editor-lite 迁移 rmvpe-infer、game-infer、audio-util 到 synthrt 的分步实施计划。每个阶段可独立编译验证、单独提交。

**v1 → v2 主要修正：**
- Phase 1 改为基于 dataset-tools FFmpeg 迁移（非 audio-util）
- 移除 SndfileVio/FlacDecoder/Mp3Decoder/MathUtils 迁移（被 FFmpeg 替代）
- Phase 2 修正：不需要 ModuleCategory 注册，不需要 ExtractSetup
- Phase 3/4 修正：Slicer 参数、createImpl、MidiExtractOptions
- Phase 5 修正：lite 用 PluginFactory.addPluginPath 而非 moduleCategory("extract")
- 移除 ExtractSetup，改为 PluginFactory addPluginPath

---

## 1. 迁移阶段总览

| 阶段 | 内容 | 依赖 | 提交 | 可并行 |
|---|---|---|---|---|
| Phase 1 | lib/Audio 模块搭建 + FFmpeg 迁移 + Slicer 迁移 | 无 | 1 个提交 | ✓ |
| Phase 2 | lib/Extract 模块搭建 + 接口定义 + 辅助函数 | Phase 1 | 1 个提交 | ✗（依赖 Phase 1 的 Audio 头文件） |
| Phase 3 | rmvpe 插件迁移 | Phase 2 | 1 个提交 | ✓（与 Phase 4 并行） |
| Phase 4 | game 插件迁移 | Phase 2 | 1 个提交 | ✓（与 Phase 3 并行） |
| Phase 5 | lite 适配 + 旧代码清理 | Phase 3+4 | 1 个提交 | ✗ |
| Phase 6 | 文档更新 + 集成测试 | Phase 5 | 1 个提交 | ✗ |

**并行执行策略：**
- Phase 1 和 Phase 2 可以用子代理并行启动，但 Phase 2 编译依赖 Phase 1（需先完成 Phase 1 的头文件）
- Phase 3 和 Phase 4 可以完全并行（独立插件，互不依赖）
- 建议用 2 个子代理分别执行 Phase 1 和 Phase 2，Phase 1 完成后 Phase 2 即可编译

---

## 2. Phase 1: lib/Audio 模块

### 2.1 任务清单

- [x] 创建 `lib/Audio/` 目录结构
- [x] 创建 `lib/Audio/include/synthrt/Audio/` 公开头文件
  - [x] `srt_audio_global.h`（SRT_AUDIO_EXPORT 导出宏）
  - [x] `SampleFormat.h`（枚举 + bytesPerSample 内联函数）
  - [x] `AudioBuffer.h`（view/owned 语义，从 dataset-tools 迁移）
  - [x] `AudioFormatInfo.h`（格式探测信息）
  - [x] `ResampleConfig.h`（重采样配置 + ResampleQuality）
  - [x] `IAudioDecoder.h`（解码器抽象接口）
  - [x] `IAudioResampler.h`（重采样器抽象接口）
  - [x] `FfmpegAudioDecoder.h`（PIMPL 实现声明）
  - [x] `SwresampleResampler.h`（PIMPL 实现声明）
  - [x] `AudioPipeline.h`（解码+重采样流水线）
  - [x] `AudioFileWriter.h`（PIMPL 文件写入器）
  - [x] `Slicer.h`（从 audio-util 迁移，RMS 静音切片）
- [x] 创建 `lib/Audio/src/` 实现
  - [x] `AudioBuffer.cpp`（从 dataset-tools `src/AudioBuffer.cpp` 迁移）
  - [x] `FfmpegAudioDecoder.cpp`（从 dataset-tools `src/FfmpegAudioDecoder.cpp` 迁移）
  - [x] `SwresampleResampler.cpp`（从 dataset-tools `src/SwresampleResampler.cpp` 迁移）
  - [x] `AudioPipeline.cpp`（从 dataset-tools `src/AudioPipeline.cpp` 迁移）
  - [x] `AudioFileWriter.cpp`（从 dataset-tools `src/AudioFileWriter.cpp` 迁移）
  - [x] `Slicer.cpp`（从 lite `src/libs/audio-util/src/Slicer.cpp` 迁移）
  - [x] `FfmpegUtils.h`（从 dataset-tools `src/FfmpegUtils.h` 迁移，内部头文件）
- [x] 创建 `lib/Audio/CMakeLists.txt`
- [x] 修改根 `CMakeLists.txt`：在 `lib/Core` 之后新增 `add_subdirectory(lib/Audio)`
- [x] 修改 `scripts/vcpkg-manifest/vcpkg.json`：新增 `ffmpeg-fake` 组件（avformat/avcodec/avutil/swresample）
- [x] 在 `include/synthrt/Core/Support/Diagnostic.h` 新增 Audio 错误码段（700-799）
- [ ] 创建 `lib/Audio/unittests/` 单元测试（Phase 1 暂不创建，后续补充）
  - [ ] `test_audio_buffer.cpp`
  - [ ] `test_audio_decoder.cpp`
  - [ ] `test_audio_resampler.cpp`
  - [ ] `test_audio_pipeline.cpp`
  - [ ] `test_slicer.cpp`

### 2.2 迁移改造要点

**namespace 变更：**
```cpp
// dataset-tools
namespace dsfw::audio { ... }

// synthrt
namespace srt::audio { ... }
```

**Result → Expected 映射：**

| dataset-tools | synthrt | 说明 |
|---|---|---|
| `dsfw::Result<T>::Error(msg)` | `srt::core::Error(code, msg)` | 需指定 ErrorCode |
| `dsfw::Result<T>::Ok(val)` | `srt::core::Expected<T>(val)` | 直接构造 |
| `result.value()` | `exp.take()` | 取值 |
| `result.error()` | `exp.error().message()` | 取错误消息 |
| `if (!result)` | `if (!exp)` | 判断失败 |

**依赖变更：**
- 移除 `dsfw::types` 依赖（Result 类型）
- 移除 SDL2 依赖（不需要 playback 模块）
- 不迁移 `playback/` 子目录
- 新增 FFmpeg 依赖（avformat/avcodec/avutil/swresample）

**AudioBuffer 适配：**
- namespace: `dsfw::audio` → `srt::audio`
- 不依赖 `dsfw::types`
- `durationSec(int sampleRate)` 方法保持（需传入采样率，因为 AudioBuffer 不存储采样率）
- `std::variant<std::vector<uint8_t>, std::span<const uint8_t>>` 保持 view/owned 双语义

**Slicer 适配（从 audio-util 迁移）：**
- namespace: `AudioUtil` → `srt::audio`
- 导出宏: `AUDIO_UTIL_EXPORT` → `SRT_AUDIO_EXPORT`
- 可选 xsimd: `AUDIOUTIL_ENABLE_XSIMD` → `SRT_AUDIO_ENABLE_XSIMD`
- MarkerList 类型: `std::vector<std::pair<int64_t, int64_t>>`（保持不变）
- 构造参数: `(int sampleRate, float threshold, int hopSize, int winSize, int minLength, int minInterval, int maxSilKept)`（保持不变）

**不迁移的 audio-util 组件：**
- `SndfileVio` — FFmpeg-based 不需要内存 VIO
- `Util`（resample_to_vio）— 被 AudioPipeline.decodeAndResample 替代
- `FlacDecoder` — FFmpeg 解码 FLAC
- `Mp3Decoder` — FFmpeg 解码 MP3
- `MathUtils` — 不再需要

### 2.3 CMake 配置

```cmake
# lib/Audio/CMakeLists.txt
project(synthrt-audio VERSION ${SYNTHRT_VERSION} LANGUAGES CXX)

# 引入 BuildAPI（生成 srt_audio_add_library 宏）
# 跟随 synthrt 根 CMakeLists.txt 的 BuildAPI 配置

file(GLOB_RECURSE _src
    include/*.h
    src/*.h
    src/*.cpp
)

srt_audio_add_library(${PROJECT_NAME} SHARED
    NAMESPACE srt::audio
    SOURCES ${_src}
    FEATURES cxx_std_20
    DEPENDS
        PUBLIC srt::core
    LINKS_PRIVATE
        ${FFMPEG_LIBRARIES}
)

target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})

# 可选: xsimd SIMD 加速（Slicer RMS 计算）
find_package(xsimd CONFIG)
if(xsimd_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE xsimd)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SRT_AUDIO_ENABLE_XSIMD)
endif()
```

**FFmpeg 依赖查找（在根 CMakeLists.txt 或 lib/Audio/CMakeLists.txt）：**
```cmake
find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec avutil swresample)
```

**根 CMakeLists.txt 修改：**
```cmake
add_subdirectory(lib/Core)
add_subdirectory(lib/Audio)    # ← 新增
add_subdirectory(lib/Driver)
# ...
```

### 2.4 ErrorCode 扩展

```cpp
// include/synthrt/Core/Support/ErrorCode.h
AudioDecodeFailed = 700,        // 音频解码失败
AudioResampleFailed = 701,      // 重采样失败
AudioUnsupportedFormat = 702,   // 不支持的音频格式
AudioInvalidBuffer = 703,       // 无效的 AudioBuffer
AudioWriteFailed = 704,         // 音频文件写入失败
```

### 2.5 验证标准

- [ ] `synthrt-audio` target 编译成功
- [ ] 单元测试通过（AudioBuffer view/owned、FFmpeg 解码、重采样、切片）
- [ ] 现有 synthrt 构建不受影响（Audio 模块是新增，不修改现有代码）
- [ ] vcpkg FFmpeg 依赖安装成功
- [ ] FFmpeg C 头文件不暴露到公开 API（PIMPL 核对）

---

## 3. Phase 2: lib/Extract 模块

### 3.1 任务清单

- [x] 创建 `lib/Extract/` 目录结构
- [x] 创建 `lib/Extract/include/synthrt/Extract/` 公开头文件
  - [x] `srt_extract_global.h`（SRT_EXTRACT_EXPORT 导出宏）
  - [x] `PitchExtractor.h`（PitchFrame + PitchResult + AudioRequirements + ProgressCallback + PitchExtractor 接口）
  - [x] `MidiExtractor.h`（MidiNote + MidiResult + MidiExtractOptions + MidiExtractor 接口）
  - [x] `PitchExtractorPlugin.h`（IID 模式插件工厂）
  - [x] `MidiExtractorPlugin.h`（IID 模式插件工厂）
  - [x] `ExtractorDriver.h`（getInferenceDriver 辅助函数）
  - [x] `AudioPreprocessor.h`（重采样 + 切片编排）
- [x] 创建 `lib/Extract/src/` 实现
  - [x] `ExtractorDriver.cpp`（从 rmvpe/game 的 getInferenceDriver 合并，去掉 arch 检查）
  - [x] `AudioPreprocessor.cpp`（resampleToMono + prepare 实现）
  - [x] `PitchExtractorPlugin.cpp`（构造/析构空实现）
  - [x] `MidiExtractorPlugin.cpp`（构造/析构空实现）
- [x] 创建 `lib/Extract/CMakeLists.txt`
- [ ] 修改根 `CMakeLists.txt`：在 `lib/Driver` 之后新增 `add_subdirectory(lib/Extract)`（Phase 1 负责）
- [ ] 修改 `plugins/CMakeLists.txt`：新增 `add_subdirectory(Extract)`（Phase 1 负责）
- [x] 创建 `plugins/Extract/CMakeLists.txt`（空壳，子目录在 Phase 3/4 填充）
- [ ] 在 `ErrorCode.h` 新增 Extract 错误码段（800-899）（后续统一添加）
- [ ] 创建 `lib/Extract/unittests/` 单元测试（后续补充）
  - [ ] `test_audio_preprocessor.cpp`
  - [ ] `test_extractor_driver.cpp`

### 3.2 关键实现细节

**ExtractorDriver::getInferenceDriver 实现（从 RmvpeModel.cpp:13-44 迁移，去掉 arch 检查）：**

```cpp
// lib/Extract/src/ExtractorDriver.cpp
#include <synthrt/Extract/ExtractorDriver.h>
#include <synthrt/Core/Module/ModuleCategory.h>
#include <synthrt/Core/Support/Error.h>

namespace srt::extract {

srt::core::Expected<srt::core::NO<srt::driver::InferenceDriver>>
getInferenceDriver(const srt::core::Runtime *runtime) {
    if (!runtime) {
        return srt::core::Error(srt::core::ErrorCode::SessionError, "Runtime is nullptr");
    }
    auto cate = runtime->moduleCategory("inference");
    if (!cate) {
        return srt::core::Error(srt::core::ErrorCode::DriverNotFound,
            "inference category not found");
    }
    auto obj = cate->getFirstObject("dsdriver");
    if (!obj) {
        return srt::core::Error(srt::core::ErrorCode::DriverNotFound,
            "dsdriver not found");
    }
    auto driver = obj.as<srt::driver::InferenceDriver>();
    // 只检查 backend，不检查 arch（提取器不是 diffsinger）
    if (driver->backend() != srt::driver::onnx::API_NAME) {
        return srt::core::Error(srt::core::ErrorCode::DriverNotFound,
            "backend is not onnx");
    }
    return driver;
}

} // namespace srt::extract
```

**AudioPreprocessor::resampleToMono 实现：**

```cpp
// lib/Extract/src/AudioPreprocessor.cpp
srt::core::Expected<std::pair<std::vector<float>, int>>
AudioPreprocessor::resampleToMono(
    const srt::audio::AudioBuffer &buffer,
    int sampleRate,
    const AudioRequirements &requirements) {

    // 1. 配置重采样到模型所需格式
    srt::audio::ResampleConfig config;
    config.targetSampleRate = requirements.sampleRate;
    config.targetChannelCount = requirements.channels;  // 通常为 1
    config.targetFormat = srt::audio::SampleFormat::Float32;
    config.quality = srt::audio::ResampleQuality::High;

    // 2. 执行重采样
    srt::audio::SwresampleResampler resampler;
    auto resampledExp = resampler.convert(buffer, sampleRate, config);
    if (!resampledExp) {
        return resampledExp.takeError();
    }
    auto resampled = resampledExp.take();

    // 3. 提取 float 数据
    auto floats = resampled.floats();
    std::vector<float> samples(floats.begin(), floats.end());

    return std::make_pair(std::move(samples), requirements.sampleRate);
}
```

**AudioPreprocessor::prepare 实现：**

```cpp
srt::core::Expected<std::vector<AudioPreprocessor::Slice>>
AudioPreprocessor::prepare(
    const srt::audio::AudioBuffer &buffer,
    int sampleRate,
    const AudioRequirements &requirements,
    const srt::audio::Slicer &slicer) {

    // 1. 重采样到单声道
    auto monoExp = resampleToMono(buffer, sampleRate, requirements);
    if (!monoExp) return monoExp.takeError();
    auto [samples, outSampleRate] = monoExp.take();

    // 2. 切片
    auto markers = slicer.slice(samples);

    // 3. 构造 Slice 列表
    std::vector<Slice> slices;
    for (const auto &[start, end] : markers) {
        Slice slice;
        slice.startFrame = start;
        slice.endFrame = end;
        slice.samples.assign(samples.begin() + start, samples.begin() + end);
        slices.push_back(std::move(slice));
    }
    return slices;
}
```

### 3.3 CMake 配置

```cmake
# lib/Extract/CMakeLists.txt
project(synthrt-extract VERSION ${SYNTHRT_VERSION} LANGUAGES CXX)

file(GLOB_RECURSE _src
    include/*.h
    src/*.h
    src/*.cpp
)

srt_extract_add_library(${PROJECT_NAME} SHARED
    NAMESPACE srt::extract
    SOURCES ${_src}
    FEATURES cxx_std_20
    DEPENDS
        PUBLIC srt::core srt::audio srt::driver
)
```

```cmake
# plugins/Extract/CMakeLists.txt（空壳）
# Phase 3/4 填充：
# add_subdirectory(rmvpe)
# add_subdirectory(game)
```

### 3.4 ErrorCode 扩展

```cpp
// include/synthrt/Core/Support/ErrorCode.h
ExtractNotInitialized = 800,       // 提取器未初始化
ExtractModelOpenFailed = 801,      // 模型打开失败
ExtractInferenceFailed = 802,      // 推理失败
ExtractOutputInvalid = 803,        // 输出无效
ExtractPluginNotFound = 804,       // 提取器插件未找到
ExtractUnsupportedVersion = 805,   // 不支持的模型版本
```

### 3.5 验证标准

- [ ] `synthrt-extract` target 编译成功
- [ ] `plugins/Extract/` 目录存在但为空壳
- [ ] 单元测试通过（AudioPreprocessor 重采样+切片流程、getInferenceDriver）
- [ ] 现有 synthrt 构建不受影响
- [ ] 不需要修改 Runtime 核心（不注册 ModuleCategory）

---

## 4. Phase 3: rmvpe 插件迁移

### 4.1 任务清单

- [x] 创建 `plugins/Extract/rmvpe/` 目录
- [x] 创建插件入口
  - [x] `main.cpp`（`SRT_EXPORT_PLUGIN(RmvpePlugin)`）
  - [x] `RmvpePlugin.h/cpp`（PitchExtractorPlugin 实现，key="rmvpe"）
- [x] 创建提取器实现（简化为单文件实现，无 internal/V1 子目录，当前只有 V1 版本）
  - [x] `RmvpeExtractor.h/cpp`（继承 PitchExtractor，合并版本路由壳 + V1 实现）
  - [x] ~~`internal/RmvpeExtractorBase.h/cpp`~~（合并到 RmvpeExtractor）
  - [x] ~~`internal/V1/RmvpeExtractorV1.h/cpp`~~（合并到 RmvpeExtractor）
- [x] 创建 `plugins/Extract/rmvpe/CMakeLists.txt`
- [x] 迁移 f0 插值逻辑（`interp_f0` → `interpF0` 静态方法）
- [x] 迁移 forward 逻辑（ONNX session → f0 + uv）
- [x] 适配 AudioPreprocessor（重采样 + 切片）
- [x] 错误处理改为 Expected<T>
- [x] 更新 `plugins/Extract/CMakeLists.txt` 添加 `add_subdirectory(rmvpe)`

### 4.2 迁移映射

| lite 源文件 | synthrt 目标文件 | 变更 |
|---|---|---|
| `Rmvpe.h` | `RmvpeExtractor.h` | 接口改为 PitchExtractor |
| `Rmvpe.cpp` | `RmvpeExtractor.cpp` | 合并版本路由 + V1 实现（简化为单文件） |
| `RmvpeModel.h` | `RmvpeExtractor.h` | 合并到 RmvpeExtractor（无版本子目录） |
| `RmvpeModel.cpp` | `RmvpeExtractor.cpp` | 适配新接口 |
| `getInferenceDriver()` | 复用 `srt::extract::getInferenceDriver()` | arch 检查移除 |
| `resample_to_vio()` | `AudioPreprocessor::prepare()` | 通过 AudioPreprocessor（重采样+切片） |
| `Slicer(160, ...)` | `Slicer(16000, 0.02f, 160, 640, 500, 30, 50)` | 第一参数修正为 16000（sampleRate） |
| `interp_f0()` | `RmvpeExtractor::interpF0()` | 静态方法迁移 |
| `bool get_f0(..., msg)` | `Expected<PitchResult> extract(...)` | 错误处理改进 |

### 4.3 RmvpeExtractorV1 关键实现

```cpp
// plugins/Extract/rmvpe/internal/V1/RmvpeExtractorV1.cpp
srt::core::Expected<srt::extract::PitchResult>
RmvpeExtractorV1::extract(const srt::audio::AudioBuffer &buffer,
                          int sampleRate,
                          const srt::extract::ProgressCallback &progress) {
    // 1. 重采样到 16000Hz mono
    auto req = audioRequirements();  // {16000, 1}
    auto monoExp = srt::extract::AudioPreprocessor::resampleToMono(
        buffer, sampleRate, req);
    if (!monoExp) return monoExp.takeError();
    auto [audio, outSampleRate] = monoExp.take();

    // 2. RMS 切片（参数从 Rmvpe.cpp:117 迁移，修正 sampleRate）
    // 原代码: Slicer(160, 0.02f, 160, 160*4, 500, 30, 50)
    // 第一参数原为 160（实际是 hopSize 被误传为 sampleRate）
    // 正确应为 16000（sampleRate）
    srt::audio::Slicer slicer(16000, 0.02f, 160, 160 * 4, 500, 30, 50);
    auto markers = slicer.slice(audio);

    // 3. 逐切片推理
    srt::extract::PitchResult result;
    constexpr float threshold = 0.03f;
    const int totalSlices = static_cast<int>(markers.size());

    for (int i = 0; i < totalSlices; ++i) {
        const auto &[start, end] = markers[i];

        srt::extract::PitchFrame frame;
        frame.offset = static_cast<float>(
            static_cast<double>(start) / (16000.0 / 1000.0));

        std::vector<float> sliceAudio(audio.begin() + start, audio.begin() + end);
        std::vector<float> f0;
        std::vector<bool> uv;
        if (auto exp = forward(sliceAudio, threshold, f0, uv); !exp) {
            return exp.takeError();
        }
        interpF0(f0, uv);
        frame.f0 = std::move(f0);
        frame.uv = std::move(uv);
        result.frames.push_back(std::move(frame));

        // 进度回调
        if (progress) {
            progress(static_cast<int>(100.0 * (i + 1) / totalSlices));
        }
    }
    return result;
}
```

### 4.4 CMake 配置

```cmake
# plugins/Extract/rmvpe/CMakeLists.txt
project(synthrt-plugin-extract-rmvpe
    VERSION ${SYNTHRT_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

srt_extract_add_plugin(${PROJECT_NAME} PitchExtractor ${PROJECT_NAME} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_20
    LINKS srt::core srt::driver srt::audio srt::extract
    INCLUDE_PRIVATE *
)
```

### 4.5 验证标准

- [ ] rmvpe 插件 DLL 编译成功，输出到 `bin/plugins/srt-extract/PitchExtractor/`
- [ ] 用 rmvpe 模型文件测试提取功能
- [ ] 输出与 lite 原始实现一致（f0 + uv + offset）

---

## 5. Phase 4: game 插件迁移

### 5.1 任务清单

- [x] 创建 `plugins/Extract/game/` 目录
- [x] 创建插件入口
  - [x] `main.cpp`（`SRT_EXPORT_PLUGIN(GamePlugin)`）
  - [x] `GamePlugin.h/cpp`（MidiExtractorPlugin 实现，key="game"）
- [x] 创建提取器实现（简化为扁平结构，与 rmvpe 一致，不拆分 internal/V1）
  - [x] `GameExtractor.h/cpp`（继承 MidiExtractor，包含全部推理逻辑）
  - [x] ~~`internal/GameExtractorBase.h/cpp`~~（合并到 GameExtractor）
  - [x] ~~`internal/V1/GameExtractorV1.h/cpp`~~（合并到 GameExtractor）
- [x] 创建 `plugins/Extract/game/CMakeLists.txt`
- [x] 迁移 config.json 解析逻辑（读取 targetSampleRate、seg_threshold 等）
- [x] 迁移 4 个 ONNX session 管理（encoder/segmenter/estimator/bd2dur）
- [x] 迁移 MIDI 构建逻辑（`build_midi_note`、`calculateNoteTicks`）
- [x] 迁移 D3PM 时间步生成（`generate_d3pm_ts`）
- [x] 适配 AudioPreprocessor（重采样 + 切片）
- [x] 错误处理改为 Expected<T>
- [x] 更新 `plugins/Extract/CMakeLists.txt` 添加 `add_subdirectory(game)`

### 5.2 迁移映射

| lite 源文件 | synthrt 目标文件 | 变更 |
|---|---|---|
| `Game.h` | `GameExtractor.h` | 接口改为 MidiExtractor |
| `Game.cpp` | `GameExtractor.cpp` + `V1/GameExtractorV1.cpp` | 拆分版本路由 + V1 实现 |
| `GameModel.h` | `internal/GameExtractorBase.h` + `V1/GameExtractorV1.h` | 合并 |
| `GameModel.cpp` | `V1/GameExtractorV1.cpp` | 适配新接口 |
| `config.json` 解析 | `V1/GameExtractorV1::open()` | 保留逻辑 |
| `build_midi_note()` | `V1/GameExtractorV1` | 静态方法迁移 |
| `bool get_midi(..., msg)` | `Expected<MidiResult> extract(..., options)` | 错误处理改进 |
| `Slicer(tar_sr, ...)` | `Slicer(tar_sr, 0.02f, 441, 1764, 200, 30, 50)` | 保持参数 |

### 5.3 GameExtractorV1 关键实现

```cpp
// plugins/Extract/game/internal/V1/GameExtractorV1.h
class GameExtractorV1 : public GameExtractorBase {
public:
    using GameExtractorBase::GameExtractorBase;

    srt::core::Expected<void> open(const std::filesystem::path &modelPath) override;
    bool isOpen() const override { return m_encoder != nullptr; }
    void close() override;
    void terminate() override;
    srt::extract::AudioRequirements audioRequirements() const override {
        return {m_targetSampleRate, 1};  // 从 config.json 读取
    }

    srt::core::Expected<srt::extract::MidiResult> extract(
        const srt::audio::AudioBuffer &buffer,
        int sampleRate,
        const srt::extract::MidiExtractOptions &options,
        const srt::extract::ProgressCallback &progress) override;

private:
    // 4 个 ONNX session
    srt::core::NO<srt::driver::InferenceSession> m_encoder;
    srt::core::NO<srt::driver::InferenceSession> m_segmenter;
    srt::core::NO<srt::driver::InferenceSession> m_estimator;
    srt::core::NO<srt::driver::InferenceSession> m_bd2dur;

    // 从 config.json 读取的参数
    int m_targetSampleRate = 44100;
    float m_segThreshold = 0.2f;
    float m_estThreshold = 0.2f;
    int m_language = 0;

    /// D3PM 时间步生成（从 Game.cpp 迁移）
    static std::vector<float> generateD3pmTs();
    /// MIDI 音符构建（从 Game.cpp 迁移）
    static std::vector<srt::extract::MidiNote> buildMidiNotes(/* ... */);
};
```

### 5.4 CMake 配置

```cmake
# plugins/Extract/game/CMakeLists.txt
project(synthrt-plugin-extract-game
    VERSION ${SYNTHRT_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

srt_extract_add_plugin(${PROJECT_NAME} MidiExtractor ${PROJECT_NAME} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_20
    LINKS srt::core srt::driver srt::audio srt::extract
    LINKS_PRIVATE nlohmann-json::nlohmann_json
    INCLUDE_PRIVATE *
)
```

### 5.5 验证标准

- [ ] game 插件 DLL 编译成功，输出到 `bin/plugins/srt-extract/MidiExtractor/`
- [ ] 用 game 模型目录测试提取功能
- [ ] 输出与 lite 原始实现一致（MIDI 音符序列）

---

## 6. Phase 5: lite 适配

### 6.1 任务清单

- [x] 修改 lite 的 `vcpkg.json`：移除 audio 相关依赖（sndfile/soxr/mpg123/FLAC/xsimd）
- [x] 修改 lite 的 `CMakeLists.txt`：移除 `add_subdirectory(src/libs/audio-util)`、`rmvpe-infer`、`game-infer`
- [x] 修改 `SynthrtEngine`：新增 extract 插件路径注册
  - [x] 调用 `plugins->addPluginPath(kPitchExtractorPluginIid, extractPluginDir)`
  - [x] 调用 `plugins->addPluginPath(kMidiExtractorPluginIid, extractPluginDir)`
- [x] 修改 `ExtractPitchTask`：
  - [x] 移除 `#include <rmvpe-infer/Rmvpe.h>`
  - [x] 改用 `plugins->plugin<srt::extract::PitchExtractorPlugin>("rmvpe")` 获取插件
  - [x] 改用 `srt::audio::AudioPipeline::create()` 解码音频
  - [x] 结果转换：`PitchResult` → lite 内部 `QList<QPair<double, QList<double>>>`
- [x] 修改 `ExtractMidiTask`：
  - [x] 移除 `#include <game-infer/Game.h>`
  - [x] 改用 `plugins->plugin<srt::extract::MidiExtractorPlugin>("game")` 获取插件
  - [x] 配置 `MidiExtractOptions`（tempo、segThreshold、estThreshold、language）
  - [x] 结果转换：`MidiResult` → lite 内部 `std::vector<ExtractMidiNote>`
- [x] 删除 `src/libs/audio-util/` 目录
- [x] 删除 `src/libs/rmvpe-infer/` 目录
- [x] 删除 `src/libs/game-infer/` 目录
- [ ] 更新 synthrt vcpkg port（`scripts/vcpkg/ports/synthrt/`）

### 6.2 lite 初始化代码

```cpp
// lite 侧：SynthrtEngine.cpp
void SynthrtEngine::initializeExtractPlugins() {
    auto *plugins = m_runtime.services().get<srt::core::PluginFactory>();

    // 注册提取器插件搜索路径
    const auto extractPluginDir = (m_pluginRoot / "srt-extract").string();
    plugins->addPluginPath(srt::extract::kPitchExtractorPluginIid, extractPluginDir);
    plugins->addPluginPath(srt::extract::kMidiExtractorPluginIid, extractPluginDir);
}
```

### 6.3 验证标准

- [ ] lite 编译成功（无 rmvpe-infer/game-infer/audio-util 依赖）
- [ ] 音高提取功能正常
- [ ] MIDI 提取功能正常
- [x] 无残留的 `audio-util` / `rmvpe-infer` / `game-infer` 引用

---

## 7. Phase 6: 文档更新与集成测试

### 7.1 任务清单

- [ ] 更新 `docs/modules/` 新增模块文档
  - [ ] `docs/modules/audio.md`
  - [ ] `docs/modules/extract.md`
- [ ] 更新 `docs/modules/overview.md` 新增 Audio 和 Extract 模块
- [ ] 更新 `docs/architecture/source-layout.md` 新增目录
- [ ] 更新 `docs/decisions/human-decisions.md` 记录迁移决策
- [ ] 创建集成测试 `tests/extract/`
  - [ ] 端到端测试：加载 rmvpe 插件 → 解码音频 → 提取音高
  - [ ] 端到端测试：加载 game 插件 → 解码音频 → 提取 MIDI
- [ ] 更新 `.github/workflows/build.yml` CI 配置

### 7.2 验证标准

- [ ] 所有文档更新完成
- [ ] 集成测试通过
- [ ] CI 三平台构建成功

---

## 8. 风险与缓解

| 风险 | 影响 | 缓解措施 |
|---|---|---|
| FFmpeg 跨平台编译问题 | Phase 1 阻塞 | vcpkg 管理依赖，CI 验证三平台 |
| FFmpeg 版本兼容性（API 变化） | Phase 1 编译错误 | 锁定 vcpkg baseline，参考 dataset-tools 已验证的 API 用法 |
| PluginFactory 找不到插件 DLL | Phase 5 运行时失败 | 确保插件输出到 `bin/plugins/srt-extract/{category}/`，路径注册正确 |
| rmvpe/game 输出与原实现不一致 | Phase 3/4 功能回归 | 用相同模型和音频对比输出 |
| lite vcpkg port 更新延迟 | Phase 5 阻塞 | 先用源码依赖，port 更新后切换 |
| SwresampleResampler 状态管理 | Phase 2 重采样错误 | 每次 convert 重新初始化 swrCtx（参考 dataset-tools 实现） |
| AudioBuffer view 语义误用 | Phase 3/4 崩溃 | view 不可修改，需修改时用 clone() 转为 owned |

---

## 9. 提交规范

每个 Phase 单独提交，不推送。提交信息格式：

```
<type>(<scope>): <简述>

<type>: feat / fix / refactor / docs / chore
<scope>: audio / extract / rmvpe / game / lite / ci
```

示例：
```
feat(audio): add lib/Audio module with FFmpeg-based AudioBuffer, decoders, resamplers
feat(extract): add lib/Extract module with PitchExtractor/MidiExtractor interfaces
feat(rmvpe): migrate rmvpe pitch extractor to synthrt plugin
feat(game): migrate game MIDI extractor to synthrt plugin
refactor(lite): adapt ExtractPitchTask and ExtractMidiTask to synthrt extract API
chore(lite): remove migrated audio-util, rmvpe-infer, game-infer libraries
docs(extract): add migration plan and module documentation
```

完成单个任务后在本文档更新 `[x]` 标记。

---

## 10. 并行执行建议

**推荐的子代理并行执行方案：**

| 子代理 | 任务 | 依赖 |
|---|---|---|
| 子代理 A | Phase 1: lib/Audio 模块 | 无 |
| 子代理 B | Phase 2: lib/Extract 模块 | Phase 1 的头文件（可先写头文件再等编译） |
| 子代理 C | Phase 3: rmvpe 插件 | Phase 2 完成 |
| 子代理 D | Phase 4: game 插件 | Phase 2 完成 |

**执行顺序：**
1. 启动子代理 A（Phase 1）和子代理 B（Phase 2）并行
2. Phase 1 完成后，Phase 2 可编译验证
3. Phase 2 完成后，启动子代理 C（Phase 3）和子代理 D（Phase 4）并行
4. Phase 3+4 完成后，执行 Phase 5（lite 适配）
5. 最后执行 Phase 6（文档更新）

**每个子代理单独提交，不推送。**
