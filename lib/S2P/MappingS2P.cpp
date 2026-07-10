#include <synthrt/S2P/MappingS2P.h>

#include <synthrt/S2P/DirectS2P.h>

#include <istream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace srt::s2p {

    class MappingS2P::Private {
    public:
        std::unordered_map<std::string, std::string> mapping;
    };

    MappingS2P::MappingS2P() : d(std::make_unique<Private>()) {
    }

    MappingS2P::~MappingS2P() = default;

    MappingS2P::MappingS2P(MappingS2P &&) noexcept = default;

    MappingS2P &MappingS2P::operator=(MappingS2P &&) noexcept = default;

    srt::core::Expected<std::unique_ptr<MappingS2P>> MappingS2P::create(std::istream &mappingFile) {
        auto obj = std::unique_ptr<MappingS2P>(new MappingS2P());

        std::string line;
        std::size_t lineNumber = 0;

        while (std::getline(mappingFile, line)) {
            ++lineNumber;

            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            const auto tab = line.find('\t');
            if (tab == std::string::npos) {
                return srt::core::Error(srt::core::ErrorCode::S2pConversionFailed,
                    "MappingS2P parse error at line " + std::to_string(lineNumber) +
                        ": missing tab separator");
            }
            if (line.find('\t', tab + 1) != std::string::npos) {
                return srt::core::Error(srt::core::ErrorCode::S2pConversionFailed,
                    "MappingS2P parse error at line " + std::to_string(lineNumber) +
                        ": multiple tab separators");
            }
            if (tab == 0) {
                return srt::core::Error(srt::core::ErrorCode::S2pConversionFailed,
                    "MappingS2P parse error at line " + std::to_string(lineNumber) +
                        ": empty original phoneme");
            }
            if (tab + 1 == line.size()) {
                return srt::core::Error(srt::core::ErrorCode::S2pConversionFailed,
                    "MappingS2P parse error at line " + std::to_string(lineNumber) +
                        ": empty target phoneme");
            }

            const std::string_view lineView(line);
            const auto originalPhoneme = lineView.substr(0, tab);
            const auto targetPhoneme = lineView.substr(tab + 1);

            const auto inserted =
                obj->d->mapping.emplace(std::string(originalPhoneme), std::string(targetPhoneme));
            if (!inserted.second) {
                return srt::core::Error(srt::core::ErrorCode::S2pConversionFailed,
                    "MappingS2P parse error at line " + std::to_string(lineNumber) +
                        ": duplicate original phoneme");
            }
        }

        if (mappingFile.bad()) {
            return srt::core::Error(srt::core::ErrorCode::S2pConversionFailed,
                "MappingS2P parse error: failed to read mapping stream");
        }

        return obj;
    }

    std::vector<std::string> MappingS2P::convert(std::string_view pronunciation) const {
        auto phonemes = DirectS2P::convert(pronunciation);

        for (auto &phoneme : phonemes) {
            const auto it = d->mapping.find(phoneme);
            if (it != d->mapping.end()) {
                phoneme = it->second;
            }
        }

        return phonemes;
    }

}
