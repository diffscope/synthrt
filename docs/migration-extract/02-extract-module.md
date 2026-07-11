# 02 - lib/Extract 模块设计（v2 修正）

日期: 2026-07-11

定位: 在 synthrt 新建 `lib/Extract/` 模块（target `synthrt-extract`，别名 `srt::extract`，namespace `srt::extract`），作为音频特征提取器的通用基础层。

---

## 1. 模块职责

`lib/Extract` 提供提取器接口定义和辅助工具，**不含具体模型实现**：

| 组件 | 职责 |
|---|---|
| `PitchExtractor` | 音高提取器接口（f0 + uv） |
| `MidiExtractor` | MIDI 提取器接口（音符序列 + 可配置参数） |
| `PitchExtractorPlugin` | 音高提取器插件工厂（继承 `srt::core::Plugin`） |
| `MidiExtractorPlugin` | MIDI 提取器插件工厂（继承 `srt::core::Plugin`） |
| `getInferenceDriver()` | 从 Runtime 获取 ONNX 驱动辅助函数 |
| `AudioPreprocessor` | 提取器内部音频预处理编排（重采样 + 切片） |

**不在 lib/Extract 中：**
- 具体模型实现（rmvpe、game → `plugins/Extract/`）
- Qt 任务状态机（留在 lite）
- 模型路径管理 / AppOptions（留在 lite）
- 文件解码（在 `lib/Audio` 中）

**不需要 ModuleCategory：** 提取器通过 PluginFactory 按需创建，不存入 Runtime 的 ModuleCategory。ModuleCategory 用于共享单例（如 `dsdriver`），提取器是 per-task 实例。

---

## 2. 模块结构

```
lib/Extract/
  CMakeLists.txt
  include/
    synthrt/
      Extract/
        srt_extract_global.h          ← SRT_EXTRACT_EXPORT 宏
        PitchExtractor.h              ← 音高提取器接口 + PitchResult
        MidiExtractor.h               ← MIDI 提取器接口 + MidiResult
        PitchExtractorPlugin.h        ← 音高提取器插件工厂
        MidiExtractorPlugin.h         ← MIDI 提取器插件工厂
        ExtractorDriver.h             ← ONNX 驱动获取辅助
        AudioPreprocessor.h           ← 音频预处理编排
  src/
    ExtractorDriver.cpp
    AudioPreprocessor.cpp
  unittests/
    CMakeLists.txt
    test_audio_preprocessor.cpp
    test_extractor_driver.cpp
```

---

## 3. 公开 API 设计

### 3.1 PitchExtractor 接口

```cpp
// include/synthrt/Extract/PitchExtractor.h
#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Extract/srt_extract_global.h>

namespace srt::extract {

    /// 单个切片的音高提取结果
    struct PitchFrame {
        float offset = 0.0f;        ///< 时间偏移（毫秒）
        std::vector<float> f0;      ///< 基频序列（Hz）
        std::vector<bool> uv;       ///< 清浊音标志（true=浊音）
    };

    /// 音高提取结果
    struct PitchResult {
        std::vector<PitchFrame> frames;  ///< 按切片组织的音高帧
    };

    /// 模型所需音频格式
    struct AudioRequirements {
        int sampleRate = 0;   ///< 模型所需采样率
        int channels = 0;     ///< 模型所需声道数
    };

    /// 进度回调（0-100）
    using ProgressCallback = std::function<void(int)>;

    /// 音高提取器接口
    ///
    /// 所有音高提取模型（rmvpe 及未来其他算法）实现此接口。
    /// 继承 NamedObject 以支持 NO<PitchExtractor> 引用计数。
    class SRT_EXTRACT_EXPORT PitchExtractor : public srt::core::NamedObject {
    public:
        virtual ~PitchExtractor() = default;

        /// 打开模型
        virtual srt::core::Expected<void> open(const std::filesystem::path &modelPath) = 0;

        /// 是否已打开
        virtual bool isOpen() const = 0;

        /// 关闭模型，释放资源
        virtual void close() = 0;

        /// 终止当前推理
        virtual void terminate() = 0;

        /// 获取模型所需的音频格式要求
        /// 在 open() 成功后调用
        virtual AudioRequirements audioRequirements() const = 0;

        /// 提取音高
        /// @param buffer 输入音频（任意采样率/声道数，内部自动重采样）
        /// @param sampleRate 输入音频的采样率（AudioBuffer 不存储采样率）
        /// @param progress 进度回调
        virtual srt::core::Expected<PitchResult> extract(
            const srt::audio::AudioBuffer &buffer,
            int sampleRate,
            const ProgressCallback &progress = {}) = 0;
    };

} // namespace srt::extract
```

**关键设计点：**
- `extract()` 接收 `AudioBuffer` + `int sampleRate`：因为 dataset-tools 的 AudioBuffer 不存储采样率
- 继承 `NamedObject`：支持 `NO<PitchExtractor>` 引用计数（类比 `InferenceDriver`）
- `audioRequirements()`：提取器声明所需格式，内部用 `AudioPreprocessor` 自动重采样

### 3.2 MidiExtractor 接口

```cpp
// include/synthrt/Extract/MidiExtractor.h
#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Extract/srt_extract_global.h>
#include <synthrt/Extract/PitchExtractor.h>  // AudioRequirements, ProgressCallback

namespace srt::extract {

    /// 单个 MIDI 音符
    struct MidiNote {
        int note = 0;          ///< MIDI 音高编号 (0-127)
        int start = 0;         ///< 起始 tick
        int duration = 0;      ///< 持续 tick
    };

    /// MIDI 提取结果
    struct MidiResult {
        std::vector<MidiNote> notes;  ///< 提取的音符序列
    };

    /// MIDI 提取器可配置参数
    struct MidiExtractOptions {
        float tempo = 120.0f;               ///< 速度（BPM）
        float segThreshold = 0.2f;          ///< 分割阈值
        float segRadiusSeconds = 0.02f;     ///< 分割半径（秒）
        float estThreshold = 0.2f;          ///< 估计阈值
        int language = 0;                   ///< 语言（0=默认）
        std::vector<float> d3pmTs;          ///< D3PM 时间步（空则自动生成）
    };

    /// MIDI 提取器接口
    ///
    /// 所有 MIDI 提取模型（game 及未来其他算法）实现此接口。
    class SRT_EXTRACT_EXPORT MidiExtractor : public srt::core::NamedObject {
    public:
        virtual ~MidiExtractor() = default;

        virtual srt::core::Expected<void> open(const std::filesystem::path &modelPath) = 0;
        virtual bool isOpen() const = 0;
        virtual void close() = 0;
        virtual void terminate() = 0;
        virtual AudioRequirements audioRequirements() const = 0;

        /// 提取 MIDI 音符
        /// @param buffer 输入音频
        /// @param sampleRate 输入音频的采样率
        /// @param options 提取参数（tempo、阈值等）
        /// @param progress 进度回调
        virtual srt::core::Expected<MidiResult> extract(
            const srt::audio::AudioBuffer &buffer,
            int sampleRate,
            const MidiExtractOptions &options,
            const ProgressCallback &progress = {}) = 0;
    };

} // namespace srt::extract
```

**与 v1 方案的区别：**
- `extract()` 接收 `MidiExtractOptions` 而非单独的 `tempo` 参数
- 包含 Game 模型的所有可配置参数（segThreshold/estThreshold/language/d3pmTs）
- d3pmTs 为空时由插件内部自动生成（`generate_d3pm_ts()`）

### 3.3 插件工厂接口

遵循 `InferenceDriverPlugin` 模式（见 InferenceDriverPlugin.h）：

```cpp
// include/synthrt/Extract/PitchExtractorPlugin.h
#pragma once

#include <synthrt/Core/Plugin/Plugin.h>
#include <synthrt/Extract/PitchExtractor.h>
#include <synthrt/Extract/srt_extract_global.h>

namespace srt::core {
    class Runtime;
}

namespace srt::extract {

    /// IID 常量（类比 kInferenceDriverPluginIid = "srt.driver.InferenceDriver"）
    inline constexpr auto kPitchExtractorPluginIid = "srt.extract.PitchExtractor";

    /// 音高提取器插件工厂
    ///
    /// 由音高提取器插件 DLL 实现。PluginFactory 按 IID 加载，
    /// lite 通过此工厂创建 PitchExtractor 实例。
    class SRT_EXTRACT_EXPORT PitchExtractorPlugin : public srt::core::Plugin {
    public:
        PitchExtractorPlugin();
        ~PitchExtractorPlugin() override;

        const char *iid() const override { return staticIid(); }

        /// 静态 IID 访问器 — 允许 PluginFactory::plugin<T>(key) 无需实例即可获取 IID
        static const char *staticIid() { return kPitchExtractorPluginIid; }

        /// 创建音高提取器实例
        /// @param runtime Runtime 实例（提取器通过它获取 ONNX 驱动）
        /// @return 新的 PitchExtractor 实例（未 open）
        virtual srt::core::Expected<srt::core::NO<PitchExtractor>>
        createExtractor(srt::core::Runtime *runtime) = 0;

        STDCORELIB_DISABLE_COPY(PitchExtractorPlugin)
    };

} // namespace srt::extract
```

```cpp
// include/synthrt/Extract/MidiExtractorPlugin.h
#pragma once

#include <synthrt/Core/Plugin/Plugin.h>
#include <synthrt/Extract/MidiExtractor.h>
#include <synthrt/Extract/srt_extract_global.h>

namespace srt::core {
    class Runtime;
}

namespace srt::extract {

    inline constexpr auto kMidiExtractorPluginIid = "srt.extract.MidiExtractor";

    class SRT_EXTRACT_EXPORT MidiExtractorPlugin : public srt::core::Plugin {
    public:
        MidiExtractorPlugin();
        ~MidiExtractorPlugin() override;

        const char *iid() const override { return staticIid(); }
        static const char *staticIid() { return kMidiExtractorPluginIid; }

        virtual srt::core::Expected<srt::core::NO<MidiExtractor>>
        createExtractor(srt::core::Runtime *runtime) = 0;

        STDCORELIB_DISABLE_COPY(MidiExtractorPlugin)
    };

} // namespace srt::extract
```

**关键设计点：**
- `staticIid()` 返回常量 IID，允许 `PluginFactory::plugin<T>(key)` 在不实例化的情况下获取 IID
- `createExtractor(Runtime*)` 传入 Runtime：提取器需要 Runtime 来获取 ONNX 驱动（通过 `moduleCategory("inference")->getFirstObject("dsdriver")`）
- 没有 `extractorType()` 方法：IID 本身区分类型
- 继承 `Plugin`：与 `InferenceDriverPlugin` 完全一致的模式

### 3.4 ExtractorDriver 辅助

```cpp
// include/synthrt/Extract/ExtractorDriver.h
#pragma once

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Extract/srt_extract_global.h>

namespace srt::core {
    class Runtime;
}

namespace srt::extract {

    /// 从 Runtime 获取 ONNX 推理驱动
    ///
    /// 提取器插件用此函数获取已注册的 dsdriver。
    /// 只检查 backend == "onnx"，不检查 arch（提取器不是 DiffSinger 模型）。
    SRT_EXTRACT_EXPORT
    srt::core::Expected<srt::core::NO<srt::driver::InferenceDriver>>
    getInferenceDriver(const srt::core::Runtime *runtime);

} // namespace srt::extract
```

**实现逻辑（从 RmvpeModel.cpp:13-44 迁移，去掉 arch 检查）：**
```cpp
srt::core::Expected<srt::core::NO<srt::driver::InferenceDriver>>
getInferenceDriver(const srt::core::Runtime *runtime) {
    if (!runtime) {
        return Error(ErrorCode::SessionError, "Runtime is nullptr");
    }
    auto cate = runtime->moduleCategory("inference");
    if (!cate) {
        return Error(ErrorCode::DriverNotFound, "inference category not found");
    }
    auto obj = cate->getFirstObject("dsdriver");
    if (!obj) {
        return Error(ErrorCode::DriverNotFound, "dsdriver not found");
    }
    auto driver = obj.as<srt::driver::InferenceDriver>();
    // 只检查 backend，不检查 arch（提取器不是 diffsinger）
    if (driver->backend() != srt::driver::onnx::API_NAME) {
        return Error(ErrorCode::DriverNotFound, "backend is not onnx");
    }
    return driver;
}
```

### 3.5 AudioPreprocessor

```cpp
// include/synthrt/Extract/AudioPreprocessor.h
#pragma once

#include <vector>

#include <synthrt/Audio/AudioBuffer.h>
#include <synthrt/Audio/Slicer.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Extract/PitchExtractor.h>  // AudioRequirements
#include <synthrt/Extract/srt_extract_global.h>

namespace srt::extract {

    /// 音频预处理编排
    class SRT_EXTRACT_EXPORT AudioPreprocessor {
    public:
        struct Slice {
            int64_t startFrame;
            int64_t endFrame;
            std::vector<float> samples;  ///< 单声道 float PCM
        };

        /// 重采样 + 切片
        /// @param buffer 输入音频（任意格式）
        /// @param sampleRate 输入音频采样率
        /// @param requirements 模型所需格式
        /// @param slicer 切片器
        static srt::core::Expected<std::vector<Slice>> prepare(
            const srt::audio::AudioBuffer &buffer,
            int sampleRate,
            const AudioRequirements &requirements,
            const srt::audio::Slicer &slicer);

        /// 仅重采样到模型所需格式（返回 float 单声道 vector + 采样率）
        static srt::core::Expected<std::pair<std::vector<float>, int>> resampleToMono(
            const srt::audio::AudioBuffer &buffer,
            int sampleRate,
            const AudioRequirements &requirements);
    };

} // namespace srt::extract
```

**实现逻辑：**
1. 用 `SwresampleResampler` 重采样到 requirements 指定的采样率/声道数
2. 提取单声道 float 数据
3. 用 `Slicer` 切片
4. 返回 Slice 列表

---

## 4. CMake 配置

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

根 CMakeLists.txt：在 `lib/Driver` 之后新增 `add_subdirectory(lib/Extract)`。

---

## 5. ErrorCode 扩展

```cpp
ExtractNotInitialized = 800,       // 提取器未初始化
ExtractModelOpenFailed = 801,      // 模型打开失败
ExtractInferenceFailed = 802,      // 推理失败
ExtractOutputInvalid = 803,        // 输出无效
ExtractPluginNotFound = 804,       // 提取器插件未找到
ExtractUnsupportedVersion = 805,   // 不支持的模型版本
```
