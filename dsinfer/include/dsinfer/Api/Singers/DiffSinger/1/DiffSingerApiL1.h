#ifndef DSINFER_API_DIFFSINGERAPIL1_H
#define DSINFER_API_DIFFSINGERAPIL1_H

#include <synthrt/SVS/SingerContrib.h>

namespace ds::Api::DiffSinger::L1 {

    inline constexpr char API_INTERFACE[] = "org.openvpi.svs.Singer";

    inline constexpr char API_VARIANT[] = "diffsinger";

    inline constexpr int API_LEVEL = 1;

    class DiffSingerConfiguration : public srt::ContribConfiguration {
    public:
        DiffSingerConfiguration()
            : srt::ContribConfiguration(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        std::filesystem::path dict;
    };

}

#endif // DSINFER_API_DIFFSINGERAPIL1_H
