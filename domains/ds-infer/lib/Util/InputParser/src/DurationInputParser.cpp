#include "DurationInputParser.h"

#include "InputParserCommon_p.h"

namespace ds::infer {
    namespace Dur = srt::svs::Api::Duration::L1;

    srt::core::Expected<srt::core::NO<Dur::DurationStartInput>>
        parseDurationStartInput(const srt::core::JsonObject &obj) {

        auto input = srt::core::NO<Dur::DurationStartInput>::create();

        if (auto it_duration = obj.find("duration"); it_duration != obj.end()) {
            input->duration = it_duration->second.toDouble();
        }

        if (auto exp = parseWords(obj, input->words); !exp) {
            return exp.takeError();
        }

        return input;
    }
}