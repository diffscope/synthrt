#include "DurationInputParser.h"

#include "InputParserCommon_p.h"

namespace ds {
    namespace Dur = Api::Duration::L1;

    srt::Expected<std::unique_ptr<Dur::DurationStartInput>>
        parseDurationStartInput(const srt::JsonObject &obj) {

        auto input = std::make_unique<Dur::DurationStartInput>();

        if (auto it_duration = obj.find("duration"); it_duration != obj.end()) {
            input->duration = it_duration->second.toDouble();
        }

        if (auto exp = parseWords(obj, input->words); !exp) {
            return exp.takeError();
        }

        return input;
    }
}