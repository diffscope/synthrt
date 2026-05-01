#include "inferutil/Driver.h"

#include <stdcorelib/str.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

namespace ds::inferutil {
    srt::Expected<srt::NO<InferenceDriver>> getInferenceDriver(const srt::Inference *obj) {
        namespace DiffSinger = Api::DiffSinger::L1;

        auto inferenceCate = obj->spec()->SU()->category("inference");
        auto dsdriverObject = inferenceCate->getFirstObject("dsdriver");

        if (!dsdriverObject) {
            return srt::Error(srt::Error::SessionError, "could not find dsdriver");
        }

        auto driver = dsdriverObject.as<InferenceDriver>();

        const auto arch = driver->arch();
        constexpr auto expectedArch = DiffSinger::API_NAME;
        const bool isArchMatch = arch == expectedArch;

        if (!isArchMatch) {
            return srt::Error(
                srt::Error::SessionError,
                stdc::formatN(
                    R"(invalid driver: expected arch "%1", got "%2")",
                    expectedArch, arch));
        }

        return driver;
    }
}