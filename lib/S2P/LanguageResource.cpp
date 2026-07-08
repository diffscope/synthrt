#include <synthrt/S2P/LanguageResource.h>

#include <synthrt/S2P/DictionaryS2P.h>
#include <synthrt/S2P/DirectS2P.h>
#include <synthrt/S2P/RuleOnsetMarker.h>

#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace srt::s2p {

    namespace {
        std::string readAll(const std::string &path) {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                throw std::runtime_error("failed to open file: " + path);
            }
            return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        }
    }

    class LanguageResource::Private {
    public:
        enum class Mode { Direct, Dictionary };

        Mode mode = Mode::Direct;
        std::unique_ptr<DictionaryS2P> dictionary;
        std::unique_ptr<RuleOnsetMarker> onsetMarker;
    };

    LanguageResource::LanguageResource() : d(std::make_unique<Private>()) {
    }

    LanguageResource::~LanguageResource() = default;

    LanguageResource::LanguageResource(LanguageResource &&) noexcept = default;

    LanguageResource &LanguageResource::operator=(LanguageResource &&) noexcept = default;

    LanguageResource LanguageResource::direct(std::string onsetRulePath) {
        LanguageResource resource;
        resource.d->mode = Private::Mode::Direct;
        if (!onsetRulePath.empty()) {
            auto content = readAll(onsetRulePath);
            std::istringstream stream(content);
            auto marker = RuleOnsetMarker::create(stream);
            if (!marker) {
                throw std::runtime_error("failed to load onset rules: " + marker.error().message());
            }
            resource.d->onsetMarker = std::move(marker.value());
        }
        return resource;
    }

    LanguageResource LanguageResource::dictionary(std::string dictionaryPath, std::string onsetRulePath) {
        LanguageResource resource = direct(std::move(onsetRulePath));
        resource.d->mode = Private::Mode::Dictionary;
        auto content = readAll(dictionaryPath);
        std::istringstream stream(content);
        auto converter = DictionaryS2P::create(stream);
        if (!converter) {
            throw std::runtime_error("failed to load dictionary: " + converter.error().message());
        }
        resource.d->dictionary = std::move(converter.value());
        return resource;
    }

    SyllablePronunciation LanguageResource::convert(std::string_view pronunciation) const {
        SyllablePronunciation result;
        if (d->mode == Private::Mode::Dictionary) {
            result.phonemes = d->dictionary->convert(pronunciation);
        } else {
            result.phonemes = DirectS2P::convert(pronunciation);
        }
        if (d->onsetMarker) {
            result.onsets = d->onsetMarker->mark(result.phonemes);
        }
        return result;
    }

}
