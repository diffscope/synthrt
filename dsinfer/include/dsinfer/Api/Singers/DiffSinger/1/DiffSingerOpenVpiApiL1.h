#ifndef DSINFER_API_SINGERS_DIFFSINGER_1_DIFFSINGEROPENVPIAPIL1_H
#define DSINFER_API_SINGERS_DIFFSINGER_1_DIFFSINGEROPENVPIAPIL1_H

#include <filesystem>

#include <synthrt/Core/ContribSpec.h>

#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

namespace ds::Api::DiffSinger::L1::OpenVpi {

    /// Identifies this implementation of the interface contract.
    inline const srt::ContribContractTriplet Triplet{Contract, "openvpi"};

    /// Contains the interpreted configuration of a DiffSinger singer contribution.
    class Configuration : public srt::ContribConfiguration {
    public:
        Configuration() : srt::ContribConfiguration(Triplet) {
        }

        /// Path of the singer pronunciation dictionary.
        ///
        /// Reserved phonemes, the phoneme tokens that a lyric may contain directly, are not part
        /// of this configuration. They are a field of the singer category and are read through
        /// SingerSpec::reservedPhonemes(). At load time the provider requires every reserved
        /// phoneme to be present in the phoneme table of each imported model and rejects a
        /// package that violates this requirement, because a token absent from a model produces
        /// silence instead of the intended marker.
        std::filesystem::path dict;
    };

}

#endif // DSINFER_API_SINGERS_DIFFSINGER_1_DIFFSINGEROPENVPIAPIL1_H
