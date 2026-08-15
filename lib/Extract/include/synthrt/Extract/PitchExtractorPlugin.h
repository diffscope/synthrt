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

        STDC_DISABLE_COPY(PitchExtractorPlugin)
    };

} // namespace srt::extract
