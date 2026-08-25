#ifndef DSINFER_API_DIFFSINGERAPIL1_H
#define DSINFER_API_DIFFSINGERAPIL1_H

#include <memory>

#include <synthrt/SVS/SingerContrib.h>
#include <synthrt/SVS/SingerExecInstance.h>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>
#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

namespace ds::Api::DiffSinger::L1 {

    /// Identifies the DiffSinger contribution contract.
    inline constexpr char API_INTERFACE[] = "org.openvpi.svs.DiffSinger";

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

    /// Creates the inference instances configured by one DiffSinger singer contribution.
    ///
    /// A provider implementing this contract must create instances derived from this class.
    class DiffSingerExecInstance : public srt::SingerExecInstance {
    public:
        /// Creates the duration inference selected by this singer.
        virtual srt::Expected<std::unique_ptr<ds::Api::Duration::L1::DurationExecInstance>>
            createDuration(const ds::Api::Duration::L1::DurationRuntimeOptions &options) = 0;

        /// Creates the pitch inference selected by this singer.
        virtual srt::Expected<std::unique_ptr<ds::Api::Pitch::L1::PitchExecInstance>>
            createPitch(const ds::Api::Pitch::L1::PitchRuntimeOptions &options) = 0;

        /// Creates the variance inference selected by this singer.
        virtual srt::Expected<std::unique_ptr<ds::Api::Variance::L1::VarianceExecInstance>>
            createVariance(const ds::Api::Variance::L1::VarianceRuntimeOptions &options) = 0;

        /// Creates the acoustic inference selected by this singer.
        virtual srt::Expected<std::unique_ptr<ds::Api::Acoustic::L1::AcousticExecInstance>>
            createAcoustic(const ds::Api::Acoustic::L1::AcousticRuntimeOptions &options) = 0;

        /// Creates the vocoder inference selected by this singer.
        virtual srt::Expected<std::unique_ptr<ds::Api::Vocoder::L1::VocoderExecInstance>>
            createVocoder(const ds::Api::Vocoder::L1::VocoderRuntimeOptions &options) = 0;

    protected:
        using SingerExecInstance::SingerExecInstance;
    };

}

#endif // DSINFER_API_DIFFSINGERAPIL1_H
