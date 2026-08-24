#include <synthrt/SVS/SingerProviderPlugin.h>

#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

#include "DiffSingerProvider.h"

namespace ds {

    class DiffSingerProviderPlugin : public srt::SingerProviderPlugin {
    public:
        DiffSingerProviderPlugin() = default;

        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            namespace DiffSinger = Api::DiffSinger::L1;
            if (interfaceName != DiffSinger::API_INTERFACE || level != DiffSinger::API_LEVEL ||
                variant != DiffSinger::API_VARIANT) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported DiffSinger provider contract");
            }
            return std::unique_ptr<srt::ContribInterpreter>(new DiffSingerProvider());
        }
    };

}

STDC_EXPORT_PLUGIN(ds::DiffSingerProviderPlugin)
