#ifndef DSINFER_API_SINGERS_DIFFSINGER_1_DIFFSINGERAPIL1_H
#define DSINFER_API_SINGERS_DIFFSINGER_1_DIFFSINGERAPIL1_H

#include <map>
#include <string>
#include <vector>

#include <synthrt/Core/ContribContract.h>
#include <synthrt/SVS/SingerContrib.h>
#include <synthrt/SVS/SingerPipelineExecutive.h>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>
#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

namespace ds::Api::DiffSinger::L1 {

    /// Identifies the DiffSinger interface contract.
    inline const srt::ContribContract Contract{"org.openvpi.dsinfer.singer.DiffSinger", 1};
    /// Describes the capabilities exported by a DiffSinger singer contribution.
    class Exports : public srt::ContribExports {
    public:
        Exports() : ContribExports(Contract) {
        }

        /// Maps language handles to the import roles that provide them.
        std::map<std::string, std::string> languages;

        /// Language handle used when a caller specifies no language.
        ///
        /// Required when \c languages is not empty, and then one of its keys.
        std::string defaultLanguage;

        /// Phoneme tokens that a lyric may contain directly and that no language produces.
        ///
        /// The provider requires every token to appear in the phoneme table of each imported
        /// model. A token absent from a model produces silence instead of the intended marker.
        std::vector<std::string> reservedPhonemes;
    };

    /// Supplies runtime settings when a DiffSinger synthesis pipeline is created.
    class PipelineRuntimeOptions : public srt::SingerPipelineRuntimeOptions {
    public:
        PipelineRuntimeOptions() : SingerPipelineRuntimeOptions(Contract) {
        }
    };

    /// Creates the inference executives configured by one DiffSinger singer contribution.
    ///
    /// A provider implementing this contract must create executives derived from this class. Each
    /// returned inference executive is owned by its pipeline. The caller may delete an executive
    /// early.
    class PipelineExecutive : public srt::SingerPipelineExecutive {
    public:
        /// Creates the duration inference selected by this singer.
        virtual srt::Expected<ds::Api::Duration::L1::Executive *>
            createDuration(const ds::Api::Duration::L1::RuntimeOptions &options) = 0;

        /// Creates the pitch inference selected by this singer.
        virtual srt::Expected<ds::Api::Pitch::L1::Executive *>
            createPitch(const ds::Api::Pitch::L1::RuntimeOptions &options) = 0;

        /// Creates the variance inference selected by this singer.
        virtual srt::Expected<ds::Api::Variance::L1::Executive *>
            createVariance(const ds::Api::Variance::L1::RuntimeOptions &options) = 0;

        /// Creates the acoustic inference selected by this singer.
        virtual srt::Expected<ds::Api::Acoustic::L1::Executive *>
            createAcoustic(const ds::Api::Acoustic::L1::RuntimeOptions &options) = 0;

        /// Creates the vocoder inference selected by this singer.
        virtual srt::Expected<ds::Api::Vocoder::L1::Executive *>
            createVocoder(const ds::Api::Vocoder::L1::RuntimeOptions &options) = 0;

    protected:
        using SingerPipelineExecutive::SingerPipelineExecutive;
    };

}

namespace srt {

    template <>
    struct ContribSpecExtensionTraits<SingerSpec, ds::Api::DiffSinger::L1::PipelineExecutive> {
        inline static constexpr char ID[] = "org.openvpi.dsinfer.extension.DiffSingerPipeline";
    };

}

#endif // DSINFER_API_SINGERS_DIFFSINGER_1_DIFFSINGERAPIL1_H
