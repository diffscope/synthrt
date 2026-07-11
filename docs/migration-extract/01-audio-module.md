# 01 - lib/Audio 模块设计（v2 FFmpeg-based）

日期: 2026-07-11

定位: 将 dataset-tools 的 `src/framework/audio/` FFmpeg-based 音频处理框架迁移到 synthrt 的 `lib/Audio/`，作为独立 target `synthrt-audio`（别名 `synthrt::audio`），namespace `srt::audio`。同时迁移 audio-util 的 RMS Slicer。

---

## 1. 迁移来源

### 1.1 从 dataset-tools 迁移（FFmpeg-based）

| 源文件 (dataset-tools) | 目标 (synthrt) | 职责 |
|---|---|---|
| `include/dsfw/audio/AudioBuffer.h` | `include/synthrt/Audio/AudioBuffer.h` | view/owned 语义音频缓冲区 |
| `include/dsfw/audio/AudioFormatInfo.h` | `include/synthrt/Audio/AudioFormatInfo.h` | 格式探测信息 |
| `include/dsfw/audio/IAudioDecoder.h` | `include/synthrt/Audio/IAudioDecoder.h` | 解码器抽象接口 |
| `include/dsfw/audio/IAudioResampler.h` | `include/synthrt/Audio/IAudioResampler.h` | 重采样器抽象接口 |
| `include/dsfw/audio/FfmpegAudioDecoder.h` | `include/synthrt/Audio/FfmpegAudioDecoder.h` | FFmpeg 解码器实现 |
| `include/dsfw/audio/SwresampleResampler.h` | `include/synthrt/Audio/SwresampleResampler.h` | libswresample 重采样实现 |
| `include/dsfw/audio/AudioPipeline.h` | `include/synthrt/Audio/AudioPipeline.h` | 解码+重采样流水线 |
| `include/dsfw/audio/AudioFileWriter.h` | `include/synthrt/Audio/AudioFileWriter.h` | 音频文件写入 |
| `include/dsfw/audio/ResampleConfig.h` | `include/synthrt/Audio/ResampleConfig.h` | 重采样配置 |
| `src/FfmpegAudioDecoder.cpp` | `lib/Audio/src/FfmpegAudioDecoder.cpp` | FFmpeg 解码实现 |
| `src/SwresampleResampler.cpp` | `lib/Audio/src/SwresampleResampler.cpp` | 重采样实现 |
| `src/AudioPipeline.cpp` | `lib/Audio/src/AudioPipeline.cpp` | 流水线实现 |
| `src/AudioBuffer.cpp` | `lib/Audio/src/AudioBuffer.cpp` | AudioBuffer 实现 |
| `src/AudioFileWriter.cpp` | `lib/Audio/src/AudioFileWriter.cpp` | 文件写入实现 |
| `src/FfmpegUtils.h` | `lib/Audio/src/FfmpegUtils.h` | FFmpeg 辅助（内部） |

### 1.2 从 ds-editor-lite audio-util 迁移（Slicer）

| 源文件 (ds-editor-lite) | 目标 (synthrt) | 职责 |
|---|---|---|
| `include/audio-util/Slicer.h` + `src/Slicer.cpp` | `include/synthrt/Audio/Slicer.h` + `lib/Audio/src/Slicer.cpp` | RMS 静音切片 |

**不迁移的 audio-util 组件：** SndfileVio、Util（resample_to_vio）、FlacDecoder、Mp3Decoder、MathUtils。这些被 FFmpeg-based 设计完全替代。

---

## 2. 模块结构

```
lib/Audio/
  CMakeLists.txt
  include/
    synthrt/
      Audio/
        srt_audio_global.h         ← SRT_AUDIO_EXPORT 宏
        AudioBuffer.h              ← view/owned 语义缓冲区
        AudioFormatInfo.h          ← 格式探测信息
        ResampleConfig.h           ← 重采样配置
        IAudioDecoder.h            ← 解码器抽象接口
        IAudioResampler.h          ← 重采样器抽象接口
        FfmpegAudioDecoder.h       ← FFmpeg 解码器（PIMPL）
        SwresampleResampler.h      ← swresample 重采样器（PIMPL）
        AudioPipeline.h            ← 解码+重采样流水线
        AudioFileWriter.h          ← 音频文件写入器（PIMPL）
        Slicer.h                   ← RMS 静音切片器
  src/
    AudioBuffer.cpp
    FfmpegAudioDecoder.cpp
    SwresampleResampler.cpp
    AudioPipeline.cpp
    AudioFileWriter.cpp
    Slicer.cpp
    FfmpegUtils.h                  ← FFmpeg 辅助（内部，不公开）
  unittests/
    CMakeLists.txt
    test_audio_buffer.cpp
    test_audio_decoder.cpp
    test_audio_resampler.cpp
    test_audio_pipeline.cpp
    test_slicer.cpp
```

---

## 3. 迁移改造要点

### 3.1 namespace 变更

```cpp
// dataset-tools
namespace dsfw::audio { ... }

// synthrt
namespace srt::audio { ... }
```

### 3.2 Result → Expected 映射

dataset-tools 使用 `dsfw::Result<T>`，synthrt 使用 `srt::core::Expected<T>`。映射规则：

| dataset-tools | synthrt | 说明 |
|---|---|---|
| `dsfw::Result<T>::Error(msg)` | `srt::core::Error(code, msg)` | 需指定 ErrorCode |
| `dsfw::Result<T>::Ok(val)` | `srt::core::Expected<T>(val)` | 直接构造 |
| `result.value()` | `exp.take()` | 取值 |
| `result.error()` | `exp.error().message()` | 取错误消息 |
| `if (!result)` | `if (!exp)` | 判断失败 |

### 3.3 依赖变更

dataset-tools 的 AudioBuffer 依赖 `dsfw::types`（Result 类型）。迁移后：
- AudioBuffer 本身不依赖 Expected（纯数据结构）
- IAudioDecoder/IAudioResampler 依赖 `srt::core::Expected`
- 不再需要 `dsfw::types` 依赖

### 3.4 SDL2 依赖移除

dataset-tools 的 CMakeLists.txt 链接了 `SDL2::SDL2`（用于 playback 模块）。synthrt 不需要 playback 功能，移除 SDL2 依赖。也不迁移 `playback/` 子目录。

### 3.5 Slicer 适配

Slicer 从 audio-util 迁移，改造点：
- namespace: `AudioUtil` → `srt::audio`
- 导出宏: `AUDIO_UTIL_EXPORT` → `SRT_AUDIO_EXPORT`
- 可选 xsimd: `AUDIOUTIL_ENABLE_XSIMD` → `SRT_AUDIO_ENABLE_XSIMD`
- MarkerList 类型: `std::vector<std::pair<int64_t, int64_t>>`（保持不变）
- 构造参数: `(int sampleRate, float threshold, int hopSize, int winSize, int minLength, int minInterval, int maxSilKept)`（保持不变）

### 3.6 AudioBuffer 适配

AudioBuffer 从 dataset-tools 迁移，改造点：
- namespace: `dsfw::audio` → `srt::audio`
- 不依赖 `dsfw::types`
- `durationSec(int sampleRate)` 方法保持（需要传入采样率，因为 AudioBuffer 本身不存储采样率）

**注意：** dataset-tools 的 AudioBuffer **不存储 sampleRate**。采样率信息在 AudioFormatInfo / ResampleConfig 中。这与 v1 方案中的 AudioBuffer 设计不同。提取器需要同时管理 AudioBuffer 和其采样率。

---

## 4. CMake 配置

### 4.1 target 定义

```cmake
# lib/Audio/CMakeLists.txt
project(synthrt-audio VERSION ${SYNTHRT_VERSION} LANGUAGES CXX)

# 引入 BuildAPI（生成 srt_audio_add_library 等宏）
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

### 4.2 FFmpeg 依赖查找

```cmake
# 在根 CMakeLists.txt 或 lib/Audio/CMakeLists.txt 中
find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec avutil swresample)
```

vcpkg.json 新增：
```json
"ffmpeg"
```

**INFRA-05 依赖最小化核对：**
- FFmpeg 是**单一依赖**，替代了原来的 soxr + mpg123 + FLAC + libsndfile 四个依赖
- FFmpeg 的 libavformat/libavcodec 覆盖所有音频格式解码（WAV/MP3/FLAC/AAC/Ogg/Vorbis 等）
- libswresample 覆盖重采样和声道转换
- xsimd 为可选加速，不影响功能

### 4.3 根 CMakeLists.txt 修改

在 `lib/Core` 之后新增：

```cmake
add_subdirectory(lib/Core)
add_subdirectory(lib/Audio)    # ← 新增
add_subdirectory(lib/Driver)
# ...
```

---

## 5. ErrorCode 扩展

在 `srt::core::ErrorCode` 枚举中新增 Audio 错误码段（700-799）：

```cpp
AudioDecodeFailed = 700,        // 音频解码失败
AudioResampleFailed = 701,      // 重采样失败
AudioUnsupportedFormat = 702,   // 不支持的音频格式
AudioInvalidBuffer = 703,       // 无效的 AudioBuffer
AudioWriteFailed = 704,         // 音频文件写入失败
```

---

## 6. 单元测试

| 测试文件 | 覆盖内容 |
|---|---|
| `test_audio_buffer.cpp` | create/fromCopy/fromView/fromVector、slice、clone、view 只读 |
| `test_audio_decoder.cpp` | WAV/MP3/FLAC 解码、probe、decodeAll、decodeSegment、seekToTime |
| `test_audio_resampler.cpp` | 44100→16000、mono↔stereo、format 转换、相同格式跳过、空 buffer |
| `test_audio_pipeline.cpp` | decodeAndResample、decodeSegmentAndResample、decodeToMonoFloat |
| `test_slicer.cpp` | 静音检测、切片边界、全静音、全有声、空输入 |

测试使用的音频文件放在 `unittests/fixtures/` 下。
