#ifndef DSINFER_API_DIFFSINGERAPIL1_H
#define DSINFER_API_DIFFSINGERAPIL1_H

#include <synthrt/SVS/SingerContrib.h>

namespace srt::svs::Api::DiffSinger::L1 {

    inline constexpr char API_NAME[] = "diffsinger";

    inline constexpr int API_LEVEL = 1;

    class DiffSingerConfiguration : public srt::svs::SingerConfiguration {
    public:
        DiffSingerConfiguration() : srt::svs::SingerConfiguration(API_NAME, API_LEVEL) {
        }

        std::filesystem::path dict;
    };

}

#endif // DSINFER_API_DIFFSINGERAPIL1_H