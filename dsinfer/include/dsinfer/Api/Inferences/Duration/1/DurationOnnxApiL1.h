#ifndef DSINFER_API_INFERENCES_DURATION_1_DURATIONONNXAPIL1_H
#define DSINFER_API_INFERENCES_DURATION_1_DURATIONONNXAPIL1_H

#include <string>
#include <map>
#include <vector>
#include <filesystem>

#include <synthrt/Core/ContribSpec.h>

#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>

namespace ds::Api::Duration::L1::Onnx {

    /// Selects the word level inputs declared by a duration predictor.
    enum class DurationType {
        /// Declares no word level input. A configuration that omits \c dur_type has this value and
        /// behaves like \c Abs.
        None,

        /// Declares no word level input. The predictor predicts absolute phoneme durations
        /// without the word structure of the score.
        Abs,

        /// Declares \c word_div and \c word_dur inputs. The predictor splits the frame budget of
        /// every word instead of predicting absolute durations.
        Rel,
    };

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

        /// Word level inputs declared by the predictor.
        ///
        /// A predictor that splits the frame budget of every word declares \c Rel. The task then
        /// hands it the \c word_div and \c word_dur tensors that the linguistic encoder built.
        /// Models trained before those architectures existed declare no word level input, which
        /// is the default.
        DurationType durType = DurationType::None;
    };

}

#endif // DSINFER_API_INFERENCES_DURATION_1_DURATIONONNXAPIL1_H
