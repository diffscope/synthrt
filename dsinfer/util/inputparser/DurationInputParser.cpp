#include "DurationInputParser.h"

#include "InputParserCommon_p.h"

namespace ds {

    namespace Dur = Api::Duration::L1;

    srt::Expected<std::unique_ptr<Dur::DurationStartInput>>
        parseDurationStartInput(const srt::JsonObject &obj) {

        auto input = std::make_unique<Dur::DurationStartInput>();

        if (auto result = parseOptionalNumber(obj, "duration", input->duration); !result) {
            return result.takeError();
        }

        if (auto result = parseWords(obj, input->words); !result) {
            return result.takeError();
        }

        return input;
    }

}
