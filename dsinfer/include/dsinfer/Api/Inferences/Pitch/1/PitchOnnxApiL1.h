#ifndef DSINFER_API_INFERENCES_PITCH_1_PITCHONNXAPIL1_H
#define DSINFER_API_INFERENCES_PITCH_1_PITCHONNXAPIL1_H

#include <string>
#include <map>
#include <vector>
#include <filesystem>

#include <synthrt/Core/ContribSpec.h>

#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>

namespace ds::Api::Pitch::L1::Onnx {

    /// Identifies this implementation of the interface contract.
    inline const srt::ContribContractTriplet Triplet{Contract, "onnx"};

    /// Contains the interpreted configuration of an ONNX pitch model.
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

        /// Path of the linguistic encoder model.
        std::filesystem::path encoder;

        /// Path of the pitch predictor model.
        std::filesystem::path predictor;

        /// Duration of one model frame in seconds.
        double frameWidth = 512.0 / 44100.0;

        /// Granularity accepted by the linguistic encoder.
        LinguisticMode linguisticMode = LinguisticMode::Phoneme;

        /// Width of the encoder state and each speaker embedding vector.
        int hiddenSize = 256;

        /// Indicates whether the encoder consumes language identifiers.
        bool useLanguageId = false;

        /// Indicates whether the predictor consumes speaker embeddings.
        bool useSpeakerEmbedding = false;

        /// Indicates whether the predictor consumes an expressiveness curve.
        bool useExpressiveness = true;

        /// Indicates whether the predictor consumes explicit rest flags.
        bool useRestFlags = true;

        /// Selects continuous sampling acceleration instead of discrete acceleration.
        bool useContinuousAcceleration = true;
    };

}

#endif // DSINFER_API_INFERENCES_PITCH_1_PITCHONNXAPIL1_H
