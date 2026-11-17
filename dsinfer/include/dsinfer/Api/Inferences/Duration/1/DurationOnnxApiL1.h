#ifndef DSINFER_API_INFERENCES_DURATION_1_DURATIONONNXAPIL1_H
#define DSINFER_API_INFERENCES_DURATION_1_DURATIONONNXAPIL1_H

#include <string>
#include <map>
#include <vector>
#include <filesystem>

#include <synthrt/Core/ContribSpec.h>

#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>

namespace ds::Api::Duration::L1::Onnx {

    /// Identifies this implementation of the interface contract.
    inline const srt::ContribContractTriplet Triplet{Contract, "onnx"};

    /// Contains the interpreted configuration of an ONNX duration model.
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

        /// Path of the duration predictor model.
        std::filesystem::path predictor;

        /// Duration of one model frame in seconds.
        double frameWidth = 512.0 / 44100.0;

        /// Indicates whether the encoder consumes language identifiers.
        bool useLanguageId = false;

        /// Indicates whether the predictor consumes speaker embeddings.
        bool useSpeakerEmbedding = false;

        /// Width of the encoder state and each speaker embedding vector.
        int hiddenSize = 256;

        /// Indicates whether the predictor consumes the word division of the score.
        ///
        /// A predictor that splits the frame budget of every word or that adds the position of a
        /// phoneme inside its word declares a \c word_div input. Each element of that input is the
        /// number of phonemes of one word. Models trained before those architectures existed do not
        /// declare it, and the session rejects an input the model does not expect.
        bool useWordDiv = false;

        /// Indicates whether the predictor also consumes the frame budget of every word.
        ///
        /// Declared together with \c word_div by a predictor that splits the frame budget of every
        /// word instead of predicting absolute phoneme durations. The predictor reads the budget at
        /// the word each phoneme belongs to, which it locates with the word division, so this
        /// requires \c useWordDiv.
        bool useWordDur = false;
    };

}

#endif // DSINFER_API_INFERENCES_DURATION_1_DURATIONONNXAPIL1_H
