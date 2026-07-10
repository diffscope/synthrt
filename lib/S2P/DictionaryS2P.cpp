#include <synthrt/S2P/DictionaryS2P.h>

#include <synthrt/S2P/DirectS2P.h>

#include <algorithm>
#include <istream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace srt::s2p {

    class DictionaryS2P::Private {
    public:
        std::unordered_map<std::string, std::vector<std::string>> dictionary;
    };

    DictionaryS2P::DictionaryS2P() : d(std::make_unique<Private>()) {
    }

    DictionaryS2P::~DictionaryS2P() = default;

    DictionaryS2P::DictionaryS2P(DictionaryS2P &&) noexcept = default;

    DictionaryS2P &DictionaryS2P::operator=(DictionaryS2P &&) noexcept = default;

    srt::core::Expected<std::unique_ptr<DictionaryS2P>>
    DictionaryS2P::create(std::istream &dictionaryFile) {
        auto obj = std::unique_ptr<DictionaryS2P>(new DictionaryS2P());

        std::string line;
        std::size_t lineNumber = 0;

        while (std::getline(dictionaryFile, line)) {
            ++lineNumber;

            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            const auto tab = line.find('\t');
            if (tab == std::string::npos) {
                return srt::core::Error(srt::core::ErrorCode::S2pDictionaryError,
                    "DictionaryS2P parse error at line " + std::to_string(lineNumber) +
                        ": missing tab separator");
            }
            if (line.find('\t', tab + 1) != std::string::npos) {
                return srt::core::Error(srt::core::ErrorCode::S2pDictionaryError,
                    "DictionaryS2P parse error at line " + std::to_string(lineNumber) +
                        ": multiple tab separators");
            }
            if (tab == 0) {
                return srt::core::Error(srt::core::ErrorCode::S2pDictionaryError,
                    "DictionaryS2P parse error at line " + std::to_string(lineNumber) +
                        ": empty pronunciation");
            }
            if (tab + 1 == line.size()) {
                return srt::core::Error(srt::core::ErrorCode::S2pDictionaryError,
                    "DictionaryS2P parse error at line " + std::to_string(lineNumber) +
                        ": empty phoneme sequence");
            }

            const std::string_view lineView(line);
            const auto pronunciation = lineView.substr(0, tab);
            auto phonemes = DirectS2P::convert(lineView.substr(tab + 1));

            if (std::any_of(phonemes.begin(), phonemes.end(),
                    [](const std::string &phoneme) { return phoneme.empty(); })) {
                return srt::core::Error(srt::core::ErrorCode::S2pDictionaryError,
                    "DictionaryS2P parse error at line " + std::to_string(lineNumber) +
                        ": empty phoneme");
            }

            const auto inserted =
                obj->d->dictionary.emplace(std::string(pronunciation), std::move(phonemes));
            if (!inserted.second) {
                return srt::core::Error(srt::core::ErrorCode::S2pDictionaryError,
                    "DictionaryS2P parse error at line " + std::to_string(lineNumber) +
                        ": duplicate pronunciation");
            }
        }

        if (dictionaryFile.bad()) {
            return srt::core::Error(srt::core::ErrorCode::S2pDictionaryError,
                "DictionaryS2P parse error: failed to read dictionary stream");
        }

        return obj;
    }

    std::vector<std::string> DictionaryS2P::convert(std::string_view pronunciation) const {
        const auto it = d->dictionary.find(std::string(pronunciation));
        if (it == d->dictionary.end()) {
            return {};
        }
        return it->second;
    }

}
