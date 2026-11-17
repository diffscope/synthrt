#ifndef DSINFER_API_INFERENCES_ACOUSTIC_1_ACOUSTICONNXAPIL1_H
#define DSINFER_API_INFERENCES_ACOUSTIC_1_ACOUSTICONNXAPIL1_H

#include <string>
#include <map>
#include <set>
#include <vector>
#include <filesystem>

#include <synthrt/Core/ContribSpec.h>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>

namespace ds::Api::Acoustic::L1::Onnx {

    /// Identifies this implementation of the interface contract.
    inline const srt::ContribContractTriplet Triplet{Contract, "onnx"};

    /// Contains the interpreted configuration of an ONNX acoustic model.
    class Configuration : public srt::ContribConfiguration {
    public:
        inline Configuration() : srt::ContribConfiguration(Triplet) {
        }

        /// Maps phoneme tokens to model input identifiers.
        std::map<std::string, int> phonemes;

        /// Maps language identifiers to model input identifiers.
        std::map<std::string, int> languages;

        /// Maps speaker identifiers to their embedding vectors.
        std::map<std::string, std::vector<float>> speakers;

        /// Path of the acoustic model.
        std::filesystem::path model;

        /// Indicates whether the model consumes language identifiers.
        bool useLanguageId = false;

        /// Indicates whether the model consumes speaker embeddings.
        bool useSpeakerEmbedding = false;

        /// Width of each speaker embedding vector.
        int hiddenSize = 256;

        /// Parameters consumed by the acoustic model.
        std::set<ParamTag> parameters;

        /// Selects continuous sampling acceleration instead of discrete acceleration.
        bool useContinuousAcceleration = false;

        /// Indicates whether callers may select a sampling depth.
        bool useVariableDepth = false;

        /// Maximum sampling depth accepted by the model.
        double maxDepth = 0.0;

        /// Audio sample rate in hertz.
        int sampleRate = 44100;

        /// Number of audio samples between adjacent mel frames.
        int hopSize = 2048;

        /// Analysis window size in audio samples.
        int winSize = 2048;

        /// FFT size used to construct the mel spectrogram.
        int fftSize = 128;

        /// Number of mel channels produced by the model.
        int melChannels = 128;

        /// Lower mel filter frequency in hertz.
        int melMinFreq = 0;

        /// Upper mel filter frequency in hertz.
        int melMaxFreq = 0;

        /// Logarithmic base used by the mel transform.
        MelBase melBase = MelBase::E;

        /// Frequency scale used by the mel transform.
        MelScale melScale = MelScale::Slaney;
    };

}

#endif // DSINFER_API_INFERENCES_ACOUSTIC_1_ACOUSTICONNXAPIL1_H
