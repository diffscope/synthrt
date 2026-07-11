# 03 - 插件体系设计（v2 修正）

日期: 2026-07-11

定位: 定义提取器插件的 DLL 动态加载体系。严格遵循 synthrt 现有插件模式（InferenceDriverPlugin）。

---

## 1. 插件目录结构

```
plugins/Extract/
  CMakeLists.txt              ← add_subdirectory 各插件
  rmvpe/                      ← 音高提取插件
    CMakeLists.txt
    main.cpp                  ← SRT_EXPORT_PLUGIN 入口
    RmvpePlugin.h/cpp         ← PitchExtractorPlugin 实现
    RmvpeExtractor.h/cpp      ← PitchExtractor 实现（版本路由壳）
    internal/
      RmvpeExtractorBase.h/cpp   ← 版本共享逻辑
      V1/
        RmvpeExtractorV1.h/cpp   ← v1 实现（从 lite 迁移）
  game/                       ← MIDI 提取插件
    CMakeLists.txt
    main.cpp
    GamePlugin.h/cpp         ← MidiExtractorPlugin 实现
    GameExtractor.h/cpp      ← MidiExtractor 实现（版本路由壳）
    internal/
      GameExtractorBase.h/cpp
      V1/
        GameExtractorV1.h/cpp
```

---

## 2. 插件注册机制

### 2.1 PluginFactory 加载流程

提取器插件通过 PluginFactory 加载，与 InferenceDriverPlugin 模式完全一致：

```
1. lite 注册插件搜索路径:
   plugins->addPluginPath("srt.extract.PitchExtractor", extractPluginDir);
   plugins->addPluginPath("srt.extract.MidiExtractor", extractPluginDir);

2. PluginFactory 扫描 extractPluginDir 下的 DLL:
   每个 DLL 导出 srt_plugin_instance() C 符号（由 SRT_EXPORT_PLUGIN 宏生成）
   PluginFactory 按 (iid, key) 匹配

3. lite 获取插件:
   auto *plugin = plugins->plugin<PitchExtractorPlugin>("rmvpe");
   // 内部调用 plugin(T::staticIid(), "rmvpe") = plugin("srt.extract.PitchExtractor", "rmvpe")

4. 创建提取器实例:
   auto extractor = plugin->createExtractor(&runtime).take();
```

**不需要 ModuleCategory。** 与 InferenceDriver 的区别：
- InferenceDriver 是**共享单例**，创建后存入 `moduleCategory("inference")->addObject("dsdriver", driver)` 供所有推理器使用
- 提取器是 **per-task 实例**，由 lite 按需创建，不存入 ModuleCategory

### 2.2 插件 DLL 布局

```
bin/
  plugins/
    srt-extract/
      PitchExtractor/
        rmvpe.dll              ← key="rmvpe", iid="srt.extract.PitchExtractor"
      MidiExtractor/
        game.dll               ← key="game", iid="srt.extract.MidiExtractor"
```

---

## 3. 插件入口实现

### 3.1 SRT_EXPORT_PLUGIN 宏

实际宏定义在 Plugin.h:64-68：
```cpp
#define SRT_EXPORT_PLUGIN(PLUGIN_NAME)                                    \
    extern "C" STDCORELIB_DECL_EXPORT srt::core::Plugin *srt_plugin_instance() { \
        static PLUGIN_NAME _instance;                                     \
        return &_instance;                                                \
    }
```

### 3.2 rmvpe 插件入口

```cpp
// plugins/Extract/rmvpe/main.cpp
#include <synthrt/Extract/PitchExtractorPlugin.h>
#include "RmvpePlugin.h"

SRT_EXPORT_PLUGIN(RmvpePlugin)
```

```cpp
// plugins/Extract/rmvpe/RmvpePlugin.h
#pragma once

#include <synthrt/Extract/PitchExtractorPlugin.h>

class RmvpePlugin : public srt::extract::PitchExtractorPlugin {
public:
    RmvpePlugin() = default;

    const char *key() const override { return "rmvpe"; }

    srt::core::Expected<srt::core::NO<srt::extract::PitchExtractor>>
    createExtractor(srt::core::Runtime *runtime) override;
};
```

```cpp
// plugins/Extract/rmvpe/RmvpePlugin.cpp
#include "RmvpePlugin.h"
#include "RmvpeExtractor.h"

srt::core::Expected<srt::core::NO<srt::extract::PitchExtractor>>
RmvpePlugin::createExtractor(srt::core::Runtime *runtime) {
    return srt::core::NO<RmvpeExtractor>::create(runtime);
}
```

**关键点：**
- `key()` 返回 `"rmvpe"`（类比 OnnxDriverPlugin 返回 `"onnx"`）
- `createExtractor(runtime)` 传入 Runtime，提取器通过它获取 ONNX 驱动
- `NO<RmvpeExtractor>::create(runtime)` 创建实例（NamedObject 的工厂方法）

### 3.3 game 插件入口

```cpp
// plugins/Extract/game/main.cpp
#include <synthrt/Extract/MidiExtractorPlugin.h>
#include "GamePlugin.h"

SRT_EXPORT_PLUGIN(GamePlugin)
```

```cpp
// plugins/Extract/game/GamePlugin.h
#pragma once

#include <synthrt/Extract/MidiExtractorPlugin.h>

class GamePlugin : public srt::extract::MidiExtractorPlugin {
public:
    GamePlugin() = default;

    const char *key() const override { return "game"; }

    srt::core::Expected<srt::core::NO<srt::extract::MidiExtractor>>
    createExtractor(srt::core::Runtime *runtime) override;
};
```

---

## 4. RmvpeExtractor 实现

### 4.1 版本路由壳

```cpp
// plugins/Extract/rmvpe/RmvpeExtractor.h
#pragma once

#include <memory>
#include <synthrt/Extract/PitchExtractor.h>
#include "internal/RmvpeExtractorBase.h"

class RmvpeExtractor : public srt::extract::PitchExtractor {
public:
    explicit RmvpeExtractor(srt::core::Runtime *runtime);

    srt::core::Expected<void> open(const std::filesystem::path &modelPath) override;
    bool isOpen() const override;
    void close() override;
    void terminate() override;
    srt::extract::AudioRequirements audioRequirements() const override;

    srt::core::Expected<srt::extract::PitchResult> extract(
        const srt::audio::AudioBuffer &buffer,
        int sampleRate,
        const srt::extract::ProgressCallback &progress) override;

private:
    std::unique_ptr<RmvpeExtractorBase> m_impl;

    srt::core::Expected<std::unique_ptr<RmvpeExtractorBase>>
    createImpl(const std::filesystem::path &modelPath);
};
```

### 4.2 版本共享基类

```cpp
// plugins/Extract/rmvpe/internal/RmvpeExtractorBase.h
#pragma once

#include <filesystem>
#include <vector>

#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>
#include <synthrt/Extract/PitchExtractor.h>

class RmvpeExtractorBase {
public:
    explicit RmvpeExtractorBase(srt::core::Runtime *runtime) : m_runtime(runtime) {}
    virtual ~RmvpeExtractorBase() = default;

    virtual srt::core::Expected<void> open(const std::filesystem::path &modelPath) = 0;
    virtual bool isOpen() const = 0;
    virtual void close() = 0;
    virtual void terminate() = 0;
    virtual srt::extract::AudioRequirements audioRequirements() const = 0;
    virtual srt::core::Expected<srt::extract::PitchResult> extract(
        const srt::audio::AudioBuffer &buffer,
        int sampleRate,
        const srt::extract::ProgressCallback &progress) = 0;

protected:
    srt::core::Runtime *m_runtime = nullptr;
    srt::core::NO<srt::driver::InferenceDriver> m_driver;
    srt::core::NO<srt::driver::InferenceSession> m_session;

    /// f0 插值（从 Rmvpe.cpp:35-99 迁移）
    static void interpF0(std::vector<float> &f0, std::vector<bool> &uv);
};
```

### 4.3 V1 实现

```cpp
// plugins/Extract/rmvpe/internal/V1/RmvpeExtractorV1.h
#pragma once

#include "../RmvpeExtractorBase.h"

class RmvpeExtractorV1 : public RmvpeExtractorBase {
public:
    using RmvpeExtractorBase::RmvpeExtractorBase;

    srt::core::Expected<void> open(const std::filesystem::path &modelPath) override;
    bool isOpen() const override { return m_session != nullptr; }
    void close() override;
    void terminate() override;
    srt::extract::AudioRequirements audioRequirements() const override {
        return {16000, 1};  // RMVPE 固定 16000Hz mono
    }

    srt::core::Expected<srt::extract::PitchResult> extract(
        const srt::audio::AudioBuffer &buffer,
        int sampleRate,
        const srt::extract::ProgressCallback &progress) override;

private:
    /// ONNX forward（从 RmvpeModel.cpp:132-177 迁移）
    srt::core::Expected<void> forward(
        const std::vector<float> &waveform, float threshold,
        std::vector<float> &f0, std::vector<bool> &uv);
};
```

### 4.4 V1 extract() 实现

```cpp
srt::core::Expected<srt::extract::PitchResult>
RmvpeExtractorV1::extract(const srt::audio::AudioBuffer &buffer,
                           int sampleRate,
                           const srt::extract::ProgressCallback &progress) {
    // 1. 重采样到 16000Hz mono
    auto req = audioRequirements();
    auto resampledExp = srt::extract::AudioPreprocessor::resampleToMono(
        buffer, sampleRate, req);
    if (!resampledExp) return resampledExp.takeError();
    auto [audio, outSampleRate] = resampledExp.take();

    // 2. RMS 切片（参数从 Rmvpe.cpp:117 迁移）
    // 原代码: Slicer(160, 0.02f, 160, 160*4, 500, 30, 50)
    // 第一参数原为 160（实际是 hopSize 被误传为 sampleRate）
    // 正确应为 16000（sampleRate）
    srt::audio::Slicer slicer(16000, 0.02f, 160, 160 * 4, 500, 30, 50);
    auto slicesExp = srt::extract::AudioPreprocessor::prepare(
        buffer, sampleRate, req, slicer);
    // 注: prepare 内部会先重采样再切片
    if (!slicesExp) return slicesExp.takeError();
    const auto &slices = slicesExp.take();

    // 3. 逐切片推理
    srt::extract::PitchResult result;
    constexpr float threshold = 0.03f;
    // ... 进度计算和推理（从 Rmvpe.cpp:126-158 迁移）
    for (const auto &slice : slices) {
        srt::extract::PitchFrame frame;
        frame.offset = static_cast<float>(
            static_cast<double>(slice.startFrame) / (16000.0 / 1000));

        std::vector<float> f0;
        std::vector<bool> uv;
        if (auto exp = forward(slice.samples, threshold, f0, uv); !exp) {
            return exp.takeError();
        }
        interpF0(f0, uv);
        frame.f0 = std::move(f0);
        frame.uv = std::move(uv);
        result.frames.push_back(std::move(frame));
        // ... 进度回调
    }
    return result;
}
```

---

## 5. GameExtractor 实现

结构类似 RmvpeExtractor，但实现 `MidiExtractor` 接口：

```cpp
// plugins/Extract/game/GameExtractor.h
class GameExtractor : public srt::extract::MidiExtractor {
    // 版本路由壳，同 RmvpeExtractor 模式
};
```

Game V1 的 `audioRequirements()` 从 config.json 读取：
```cpp
srt::extract::AudioRequirements GameExtractorV1::audioRequirements() const {
    return {m_targetSampleRate, 1};  // 从 config.json 读取，默认 44100
}
```

Game V1 的 `extract()` 迁移 Game.cpp:90-166 的逻辑：
- 重采样 + 切片（参数 `Slicer(tar_sr, 0.02f, 441, 441*4, 200, 30, 50)`）
- 逐切片 forward（4 个 ONNX session: encoder/segmenter/estimator/bd2dur）
- `build_midi_note()` 构建 MIDI 音符
- `calculateNoteTicks()` 计算 tick

---

## 6. CMake 配置

### 6.1 srt_extract_add_plugin 宏

由 BuildAPI.cmake 为 `synthrt-extract` 项目自动生成 `srt_extract_add_plugin()` 宏（类比 `srt_driver_add_plugin`、`srt_g2p_add_plugin`）。

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

**参数说明：**
- `PitchExtractor` / `MidiExtractor`：插件子目录名（决定输出路径 `plugins/srt-extract/PitchExtractor/`）
- `${PROJECT_NAME}`：插件文件夹名（决定 DLL 名）
- `NO_EXPORT`：不导出 CMake target（与现有插件一致）

### 6.2 plugins/CMakeLists.txt

```cmake
# plugins/CMakeLists.txt
add_subdirectory(Driver)
add_subdirectory(G2P)
add_subdirectory(diffsinger)
add_subdirectory(Extract)    # ← 新增
```

```cmake
# plugins/Extract/CMakeLists.txt
add_subdirectory(rmvpe)
add_subdirectory(game)
```

### 6.3 插件输出路径

`srt_extract_add_plugin` 宏（BuildAPI.cmake:302-315）设置输出路径为：
```
${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/plugins/srt-extract/{category}/{folder}/
```

即：
```
bin/plugins/srt-extract/PitchExtractor/synthrt-plugin-extract-rmvpe.dll
bin/plugins/srt-extract/MidiExtractor/synthrt-plugin-extract-game.dll
```

---

## 7. 多版本支持

### 7.1 版本路由

单插件 DLL 内部支持多版本，通过模型路径/config.json 判断版本：

```cpp
srt::core::Expected<std::unique_ptr<RmvpeExtractorBase>>
RmvpeExtractor::createImpl(const std::filesystem::path &modelPath) {
    // 版本判断：
    // 1. 检查 modelPath 是否为目录（包含 config.json）
    // 2. 如果有 config.json，读取 version 字段
    // 3. 无 config.json 默认为 v1

    // 当前只有 V1
    return std::make_unique<RmvpeExtractorV1>(m_runtime);
}
```

### 7.2 版本不兼容处理

```cpp
return srt::core::Error(
    srt::core::ErrorCode::ExtractUnsupportedVersion,
    stdc::formatN("RmvpeExtractor: unsupported model version in %1", modelPath.string()));
```

---

## 8. 插件 ID 约定

| 插件 | IID | Key | 接口类型 | CMake category |
|---|---|---|---|---|
| rmvpe | `srt.extract.PitchExtractor` | `"rmvpe"` | PitchExtractorPlugin | `PitchExtractor` |
| game | `srt.extract.MidiExtractor` | `"game"` | MidiExtractorPlugin | `MidiExtractor` |

未来新增提取器按相同模式注册。
