#include <synthrt/SVS/SingerProviderPlugin.h>

#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

#include "DiffSingerProvider.h"

namespace ds {

    class DiffSingerProviderPlugin : public srt::SingerProviderPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribContractTriplet &triplet) override {
            if (triplet != srt::ContribContractTriplet(Api::DiffSinger::L1::API_INTERFACE,
                                                       Api::DiffSinger::L1::API_LEVEL,
                                                       Api::DiffSinger::L1::API_VARIANT)) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new DiffSingerProvider());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::DiffSingerProviderPlugin)
