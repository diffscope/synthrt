#ifndef DSINFER_API_DIFFSINGERAPIL1_H
#define DSINFER_API_DIFFSINGERAPIL1_H

#include <synthrt/SVS/SingerContrib.h>

namespace ds::Api::DiffSinger::L1 {

    /// Identifies the singer contribution contract.
    inline constexpr char API_INTERFACE[] = "org.openvpi.svs.Singer";

    /// Identifies the DiffSinger implementation variant.
    inline constexpr char API_VARIANT[] = "diffsinger";

    /// Identifies Level 1 of the singer contribution contract.
    inline constexpr int API_LEVEL = 1;

    /// Contains the interpreted configuration of a DiffSinger singer contribution.
    class DiffSingerConfiguration : public srt::ContribConfiguration {
    public:
        DiffSingerConfiguration()
            : srt::ContribConfiguration(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }

        /// Path of the singer pronunciation dictionary.
        std::filesystem::path dict;
    };

}

#endif // DSINFER_API_DIFFSINGERAPIL1_H
