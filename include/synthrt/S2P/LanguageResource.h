#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <synthrt/S2P/srt_s2p_global.h>

namespace srt::s2p {

    struct SRT_S2P_EXPORT SyllablePronunciation {
        std::vector<std::string> phonemes;
        std::vector<bool> onsets;
    };

    class SRT_S2P_EXPORT LanguageResource {
    public:
        static LanguageResource direct(std::string onsetRulePath = {});
        static LanguageResource dictionary(std::string dictionaryPath, std::string onsetRulePath = {});

        ~LanguageResource();

        LanguageResource(const LanguageResource &) = delete;
        LanguageResource &operator=(const LanguageResource &) = delete;
        LanguageResource(LanguageResource &&) noexcept;
        LanguageResource &operator=(LanguageResource &&) noexcept;

        SyllablePronunciation convert(std::string_view pronunciation) const;

    private:
        LanguageResource();

        class Private;
        std::unique_ptr<Private> d;
    };

}
