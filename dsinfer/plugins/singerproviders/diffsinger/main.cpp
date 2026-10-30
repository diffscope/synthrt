#include <synthrt/SVS/SingerProviderPlugin.h>

#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

#include "DiffSingerProvider.h"

namespace ds {

    class DiffSingerProviderPlugin : public srt::SingerProviderPlugin {
    public:
        DiffSingerProviderPlugin() = default;

        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(const srt::ContribTriplet &triplet) override {
            namespace DiffSinger = Api::DiffSinger::L1;
            if (triplet != srt::ContribTriplet(DiffSinger::API_INTERFACE, DiffSinger::API_LEVEL,
                                               DiffSinger::API_VARIANT)) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported contract: " + triplet.toString());
            }
            return std::unique_ptr<srt::ContribInterpreter>(new DiffSingerProvider());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::DiffSingerProviderPlugin)
