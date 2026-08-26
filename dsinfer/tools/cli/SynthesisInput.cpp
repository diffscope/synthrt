#include "SynthesisInput.h"

#include <fstream>
#include <iterator>
#include <utility>

#include <stdcorelib/str.h>
#include <stdcorelib/support/json.h>

#include <synthrt/Support/JSON.h>

#include <AcousticInputParser.h>

namespace ds::cli {

    srt::Expected<SynthesisInput> SynthesisInput::load(const std::filesystem::path &path) {
        std::ifstream stream(path);
        if (!stream) {
            return srt::Error(srt::Error::FileNotOpen,
                              stdc::formatN(R"(failed to open input file "%1")", path));
        }
        std::string jsonText((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());

        stdc::json::ParseError parseError;
        auto document = srt::JsonValue::fromJson(jsonText, true, &parseError);
        if (parseError) {
            return srt::Error(srt::Error::InvalidFormat, std::move(parseError.what));
        }
        if (!document.isObject()) {
            return srt::Error(srt::Error::InvalidFormat, "synthesis input must be an object");
        }

        const auto &object = document.toObject();
        const auto singer = object.find("singer");
        if (singer == object.end()) {
            return srt::Error(srt::Error::InvalidFormat, "missing singer field");
        }
        if (!singer->second.isString()) {
            return srt::Error(srt::Error::InvalidFormat, "singer field type mismatch");
        }

        SynthesisInput result;
        result.singer = singer->second.toString();
        if (result.singer.empty()) {
            return srt::Error(srt::Error::InvalidFormat, "empty singer field");
        }

        auto acoustic = parseAcousticStartInput(object);
        if (!acoustic) {
            return acoustic.takeError();
        }
        result.acoustic = acoustic.take();
        return result;
    }

}
