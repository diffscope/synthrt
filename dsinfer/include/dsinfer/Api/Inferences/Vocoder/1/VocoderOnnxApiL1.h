#ifndef DSINFER_API_INFERENCES_VOCODER_1_VOCODERONNXAPIL1_H
#define DSINFER_API_INFERENCES_VOCODER_1_VOCODERONNXAPIL1_H

#include <filesystem>

#include <synthrt/Core/ContribSpec.h>

#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

namespace ds::Api::Vocoder::L1::Onnx {

    /// Identifies this implementation of the interface contract.
    inline const srt::ContribContractTriplet Triplet{Contract, "onnx"};

    /// Contains the interpreted configuration of an ONNX vocoder model.
    class Configuration : public srt::ContribConfiguration {
    public:
        inline Configuration() : srt::ContribConfiguration(Triplet) {
        }

        /// Path of the vocoder model.
        std::filesystem::path model;

        /// Audio sample rate in hertz.
        int sampleRate = 44100;

        /// Number of audio samples between adjacent mel frames.
        int hopSize = 2048;

        /// Analysis window size in audio samples.
        int winSize = 2048;

        /// FFT size used to construct the input mel spectrogram.
        int fftSize = 128;

        /// Number of channels in the input mel spectrogram.
        int melChannels = 128;

        /// Lower mel filter frequency in hertz.
        int melMinFreq = 0;

        /// Upper mel filter frequency in hertz.
        int melMaxFreq = 0;

        /// Logarithmic base used by the mel transform.
        MelBase melBase = MelBase::E;

        /// Frequency scale used by the mel transform.
        MelScale melScale = MelScale::Slaney;

        /// Indicates whether the vocoder accepts an independently modified pitch curve.
        bool pitchControllable = false;
    };

}

#endif // DSINFER_API_INFERENCES_VOCODER_1_VOCODERONNXAPIL1_H
