#ifndef SRT_EXTRACT_MIDIEXTRACTORPLUGIN_H
#define SRT_EXTRACT_MIDIEXTRACTORPLUGIN_H

#include <synthrt/Core/Plugin/Plugin.h>
#include <synthrt/Extract/MidiExtractor.h>
#include <synthrt/Extract/srt_extract_global.h>

namespace srt::core {
    class Runtime;
}

namespace srt::extract {

    inline constexpr auto kMidiExtractorPluginIid = "srt.extract.MidiExtractor";

    /// MIDI 提取器插件工厂
    ///
    /// 由 MIDI 提取器插件 DLL 实现。PluginFactory 按 IID 加载，
    /// lite 通过此工厂创建 MidiExtractor 实例。
    class SRT_EXTRACT_EXPORT MidiExtractorPlugin : public srt::core::Plugin {
    public:
        MidiExtractorPlugin();
        ~MidiExtractorPlugin() override;

        const char *iid() const override { return staticIid(); }

        static const char *staticIid() { return kMidiExtractorPluginIid; }

        /// 创建 MIDI 提取器实例
        /// @param runtime Runtime 实例（提取器通过它获取 ONNX 驱动）
        /// @return 新的 MidiExtractor 实例（未 open）
        virtual srt::core::Expected<srt::core::NO<MidiExtractor>>
        createExtractor(srt::core::Runtime *runtime) = 0;

        STDCORELIB_DISABLE_COPY(MidiExtractorPlugin)
    };

} // namespace srt::extract

#endif // SRT_EXTRACT_MIDIEXTRACTORPLUGIN_H
