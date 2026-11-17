#include <synthrt/SVS/SingerProviderPlugin.h>

#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerOpenVpiApiL1.h>

#include "DiffSingerProvider.h"

namespace ds {

    class DiffSingerProviderPlugin : public srt::SingerProviderPlugin {
    public:
        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribContractTriplet &triplet) override {
            if (triplet != Api::DiffSinger::L1::OpenVpi::Triplet) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new DiffSingerProvider());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::DiffSingerProviderPlugin)
