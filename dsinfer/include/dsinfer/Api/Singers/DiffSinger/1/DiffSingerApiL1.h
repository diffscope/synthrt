#ifndef DSINFER_API_DIFFSINGERAPIL1_H
#define DSINFER_API_DIFFSINGERAPIL1_H

#include <synthrt/SVS/SingerContrib.h>
#include <synthrt/SVS/SingerPipelineExecutive.h>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>
#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

namespace ds::Api::DiffSinger::L1 {

    /// Identifies the DiffSinger contribution contract.
    inline constexpr char API_INTERFACE[] = "org.openvpi.dsinfer.singer.DiffSinger";

    /// Identifies the OpenVPI DiffSinger implementation variant.
    inline constexpr char API_VARIANT[] = "openvpi";

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

    /// Supplies runtime settings when a DiffSinger synthesis pipeline is created.
    class DiffSingerPipelineRuntimeOptions : public srt::SingerPipelineRuntimeOptions {
    public:
        DiffSingerPipelineRuntimeOptions()
            : SingerPipelineRuntimeOptions(API_INTERFACE, API_VARIANT, API_LEVEL) {
        }
    };

    /// Creates the inference executives configured by one DiffSinger singer contribution.
    ///
    /// A provider implementing this contract must create executives derived from this class. Each
    /// returned inference executive is owned by its pipeline. The caller may delete an executive
    /// early.
    class DiffSingerPipelineExecutive : public srt::SingerPipelineExecutive {
    public:
        /// Creates the duration inference selected by this singer.
        virtual srt::Expected<ds::Api::Duration::L1::DurationExecutive *>
            createDuration(const ds::Api::Duration::L1::DurationRuntimeOptions &options) = 0;

        /// Creates the pitch inference selected by this singer.
        virtual srt::Expected<ds::Api::Pitch::L1::PitchExecutive *>
            createPitch(const ds::Api::Pitch::L1::PitchRuntimeOptions &options) = 0;

        /// Creates the variance inference selected by this singer.
        virtual srt::Expected<ds::Api::Variance::L1::VarianceExecutive *>
            createVariance(const ds::Api::Variance::L1::VarianceRuntimeOptions &options) = 0;

        /// Creates the acoustic inference selected by this singer.
        virtual srt::Expected<ds::Api::Acoustic::L1::AcousticExecutive *>
            createAcoustic(const ds::Api::Acoustic::L1::AcousticRuntimeOptions &options) = 0;

        /// Creates the vocoder inference selected by this singer.
        virtual srt::Expected<ds::Api::Vocoder::L1::VocoderExecutive *>
            createVocoder(const ds::Api::Vocoder::L1::VocoderRuntimeOptions &options) = 0;

    protected:
        using SingerPipelineExecutive::SingerPipelineExecutive;
    };

}

namespace srt {

    template <>
    struct ContribSpecExtensionTraits<SingerSpec,
                                      ds::Api::DiffSinger::L1::DiffSingerPipelineExecutive> {
        inline static constexpr char ID[] = "org.openvpi.dsinfer.extension.DiffSingerPipeline";
    };

}

#endif // DSINFER_API_DIFFSINGERAPIL1_H
