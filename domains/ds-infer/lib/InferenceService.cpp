#include <diffsinger/Infer/InferenceService.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/SVS/Inference.h>
#include <synthrt/Core/Task/ITask.h>
#include <synthrt/Core/Tensor/ITensor.h>

#include <dsinfer/Api/Inferences/Common/1/CommonApiL1.h>
#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>
#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

namespace ds::infer {

    namespace Co = srt::svs::Api::Common::L1;
    namespace Ac = srt::svs::Api::Acoustic::L1;
    namespace Dur = srt::svs::Api::Duration::L1;
    namespace Pit = srt::svs::Api::Pitch::L1;
    namespace Var = srt::svs::Api::Variance::L1;
    namespace Vo = srt::svs::Api::Vocoder::L1;

    using srt::core::NO;

    InferenceService::InferenceService() = default;
    InferenceService::~InferenceService() = default;

    srt::core::Expected<void> InferenceService::setStages(const StageSet &stages) {
        if (!stages.duration.spec || !stages.pitch.spec || !stages.variance.spec ||
            !stages.acoustic.spec || !stages.vocoder.spec) {
            return srt::core::Error(srt::core::ErrorCode::InferenceInputInvalid,
                                    "InferenceService::setStages: a stage spec is null");
        }

        // Validate acoustic/vocoder sampleRate compatibility (mirrors CLI).
        const auto acousticConfig =
            stages.acoustic.spec->configuration().as<Ac::AcousticConfiguration>();
        const auto vocoderConfig =
            stages.vocoder.spec->configuration().as<Vo::VocoderConfiguration>();
        if (acousticConfig && vocoderConfig &&
            acousticConfig->sampleRate != vocoderConfig->sampleRate) {
            return srt::core::Error(
                srt::core::ErrorCode::InferenceInputInvalid,
                "InferenceService::setStages: acoustic and vocoder sampleRate mismatch");
        }

        m_stages = stages;
        return srt::core::Expected<void>{};
    }

    // Helper: create + initialize an Inference, returning it or an error.
    template <class InitArgs, class RuntimeOptions>
    static srt::core::Expected<NO<srt::svs::Inference>>
        createAndInit(const InferenceService::StageSpec &stage, const std::string &stageName,
                      const std::string &singerId) {
        if (!stage.spec) {
            return srt::core::Error::inferenceError(
                srt::core::ErrorCode::InferenceModelNotFound,
                "InferenceService: " + stageName + " stage not set", singerId, stageName);
        }

        NO<srt::svs::Inference> inference;
        auto createExp = stage.spec->createInference(stage.options, NO<RuntimeOptions>::create());
        if (!createExp) {
            return srt::core::Error::inferenceError(
                srt::core::ErrorCode::InferenceStartFailed,
                "failed to create " + stageName + " inference for singer \"" + singerId +
                    "\": " + createExp.error().message(),
                singerId, stageName);
        }
        inference = createExp.take();

        auto initExp = inference->initialize(NO<InitArgs>::create());
        if (!initExp) {
            return srt::core::Error::inferenceError(
                srt::core::ErrorCode::InferenceNotInitialized,
                "failed to initialize " + stageName + " inference for singer \"" + singerId +
                    "\": " + initExp.error().message(),
                singerId, stageName);
        }
        return inference;
    }

    // Helper: start an inference and extract a typed result, checking state.
    template <class StartInput, class ResultType>
    static srt::core::Expected<NO<ResultType>>
        startAndCheck(NO<srt::svs::Inference> &inference, const NO<StartInput> &input,
                      const std::string &stageName, const std::string &singerId) {
        NO<ResultType> result;
        auto startExp = inference->start(input);
        if (!startExp) {
            return srt::core::Error::inferenceError(
                srt::core::ErrorCode::InferenceStartFailed,
                "failed to start " + stageName + " inference for singer \"" + singerId +
                    "\": " + startExp.error().message(),
                singerId, stageName);
        }
        result = startExp.take().template as<ResultType>();
        if (!result) {
            return srt::core::Error::inferenceError(
                srt::core::ErrorCode::InferenceRunFailed,
                "failed to run " + stageName + " inference for singer \"" + singerId +
                    "\": result type mismatch or null result",
                singerId, stageName);
        }
        if (inference->state() == srt::core::ITask::Failed) {
            return srt::core::Error::inferenceError(
                srt::core::ErrorCode::InferenceRunFailed,
                "failed to run " + stageName + " inference for singer \"" + singerId +
                    "\": " + result->error.message(),
                singerId, stageName);
        }
        return result;
    }

    static srt::core::Expected<void> validateAllStagesSet(const StageSet &stages) {
        if (!stages.duration.spec || !stages.pitch.spec || !stages.variance.spec ||
            !stages.acoustic.spec || !stages.vocoder.spec) {
            return srt::core::Error(srt::core::ErrorCode::InferenceInputInvalid,
                                    "InferenceService::run: stages not set");
        }
        return srt::core::Expected<void>{};
    }

    InferenceResult InferenceService::run(const InferenceRequest &request) {
        InferenceResult result;
        const auto &singer = request.singerId;

        auto stagesExp = validateAllStagesSet(m_stages);
        if (!stagesExp) {
            auto err = stagesExp.takeError();
            err.appendTrace(std::source_location::current(), "InferenceService::run");
            // validateAllStagesSet constructs a bare Error without singerId;
            // recover the context from the request so downstream diagnostics
            // can attribute the failure to the singer being processed.
            err.withContext(singer);
            result.error = std::move(err);
            return result;
        }

        // Working copies mutated through the pipeline stages.
        auto words = request.words;
        auto parameters = request.parameters;
        auto speakers = request.speakers;

        // --- Stage 1: Duration ---
        {
            auto infExp = createAndInit<Dur::DurationInitArgs, Dur::DurationRuntimeOptions>(
                m_stages.duration, "duration", singer);
            if (!infExp) {
                auto err = infExp.takeError();
                err.appendTrace(std::source_location::current(), "InferenceService::run");
                result.error = err;
                return result;
            }
            auto inference = infExp.take();

            auto input = NO<Dur::DurationStartInput>::create();
            input->words = words;

            auto resExp = startAndCheck<Dur::DurationStartInput, Dur::DurationResult>(
                inference, input, "duration", singer);
            if (!resExp) {
                auto err = resExp.takeError();
                err.appendTrace(std::source_location::current(), "InferenceService::run");
                result.error = err;
                return result;
            }
            const auto &durations = resExp.value()->durations;

            // Apply: phoneme start times relative to their word start.
            size_t i = 0;
            for (auto &word : words) {
                double timeCursor = 0.0;
                for (auto &phoneme : word.phones) {
                    if (i >= durations.size())
                        break;
                    phoneme.start = timeCursor;
                    timeCursor += durations[i];
                    ++i;
                }
            }
        }

        // --- Stage 2: Pitch ---
        {
            auto infExp = createAndInit<Pit::PitchInitArgs, Pit::PitchRuntimeOptions>(
                m_stages.pitch, "pitch", singer);
            if (!infExp) {
                auto err = infExp.takeError();
                err.appendTrace(std::source_location::current(), "InferenceService::run");
                result.error = err;
                return result;
            }
            auto inference = infExp.take();

            auto input = NO<Pit::PitchStartInput>::create();
            input->words = words;
            for (const auto &param : parameters) {
                if (param.tag == Co::Tags::Pitch || param.tag == Co::Tags::Expr) {
                    input->parameters.push_back(param);
                }
            }
            input->speakers = speakers;
            input->steps = request.steps;

            auto resExp = startAndCheck<Pit::PitchStartInput, Pit::PitchResult>(inference, input,
                                                                                "pitch", singer);
            if (!resExp) {
                auto err = resExp.takeError();
                err.appendTrace(std::source_location::current(), "InferenceService::run");
                result.error = err;
                return result;
            }
            const auto &pitch = resExp.value()->pitch;
            const auto interval = resExp.value()->interval;

            // Apply: update (or insert) the pitch parameter curve.
            bool hasPitch = false;
            for (auto &param : parameters) {
                if (param.tag == Co::Tags::Pitch) {
                    param.interval = interval;
                    param.values = pitch;
                    hasPitch = true;
                }
            }
            if (!hasPitch) {
                parameters.emplace_back(Co::InputParameterInfo{Co::Tags::Pitch, pitch, interval});
            }
        }

        // --- Stage 3: Variance ---
        {
            auto infExp = createAndInit<Var::VarianceInitArgs, Var::VarianceRuntimeOptions>(
                m_stages.variance, "variance", singer);
            if (!infExp) {
                auto err = infExp.takeError();
                err.appendTrace(std::source_location::current(), "InferenceService::run");
                result.error = err;
                return result;
            }
            auto inference = infExp.take();

            const auto schema = m_stages.variance.spec->schema().as<Var::VarianceSchema>();

            auto input = NO<Var::VarianceStartInput>::create();
            input->words = words;
            for (const auto &param : parameters) {
                if (param.tag == Co::Tags::Pitch) {
                    input->parameters.push_back(param);
                    continue;
                }
                if (schema) {
                    for (const auto &prediction : schema->predictions) {
                        if (prediction == param.tag) {
                            input->parameters.push_back(param);
                        }
                    }
                }
            }
            input->speakers = speakers;
            input->steps = request.steps;

            auto resExp = startAndCheck<Var::VarianceStartInput, Var::VarianceResult>(
                inference, input, "variance", singer);
            if (!resExp) {
                auto err = resExp.takeError();
                err.appendTrace(std::source_location::current(), "InferenceService::run");
                result.error = err;
                return result;
            }

            // Apply: update existing params with predicted values, insert new ones.
            auto &predictions = resExp.value()->predictions;
            if (schema) {
                const auto nPreds = schema->predictions.size();
                std::vector<char> satisfied(nPreds, false);
                for (size_t i = 0; i < nPreds; ++i) {
                    auto it =
                        std::find_if(parameters.begin(), parameters.end(), [&](const auto &p) {
                            return p.tag == schema->predictions[i];
                        });
                    if (it == parameters.end())
                        continue;
                    for (auto &predicted : predictions) {
                        if (it->tag == predicted.tag) {
                            it->interval = predicted.interval;
                            it->values = std::move(predicted.values);
                            it->retake = std::nullopt;
                            satisfied[i] = true;
                            break;
                        }
                    }
                }
                for (size_t i = 0; i < nPreds; ++i) {
                    if (satisfied[i])
                        continue;
                    for (auto &predicted : predictions) {
                        if (predicted.tag == schema->predictions[i]) {
                            parameters.emplace_back(std::move(predicted));
                            break;
                        }
                    }
                }
            }
        }

        // --- Stage 4: Acoustic ---
        NO<srt::core::ITensor> mel;
        NO<srt::core::ITensor> f0;
        {
            auto infExp = createAndInit<Ac::AcousticInitArgs, Ac::AcousticRuntimeOptions>(
                m_stages.acoustic, "acoustic", singer);
            if (!infExp) {
                auto err = infExp.takeError();
                err.appendTrace(std::source_location::current(), "InferenceService::run");
                result.error = err;
                return result;
            }
            auto inference = infExp.take();

            // Add default transition controls if not provided (mirrors CLI).
            const auto acConfig = m_stages.acoustic.spec->configuration().as<Ac::AcousticConfiguration>();
            if (acConfig) {
                const auto &cfgParams = acConfig->parameters;
                auto hasParam = [&](const srt::svs::ParamTag &tag) {
                    return std::any_of(parameters.begin(), parameters.end(),
                                       [&](const auto &p) { return p.tag == tag; });
                };
                if (cfgParams.find(Co::Tags::Gender) != cfgParams.end() &&
                    !hasParam(Co::Tags::Gender)) {
                    parameters.push_back({Co::Tags::Gender, {0.0}, request.duration});
                }
                if (cfgParams.find(Co::Tags::Velocity) != cfgParams.end() &&
                    !hasParam(Co::Tags::Velocity)) {
                    parameters.push_back({Co::Tags::Velocity, {1.0}, request.duration});
                }
            }

            auto input = NO<Ac::AcousticStartInput>::create();
            input->words = words;
            input->parameters = parameters;
            input->speakers = speakers;
            input->duration = request.duration;
            input->steps = request.steps;
            input->depth = request.depth;

            auto resExp = startAndCheck<Ac::AcousticStartInput, Ac::AcousticResult>(
                inference, input, "acoustic", singer);
            if (!resExp) {
                auto err = resExp.takeError();
                err.appendTrace(std::source_location::current(), "InferenceService::run");
                result.error = err;
                return result;
            }
            mel = resExp.value()->mel;
            f0 = resExp.value()->f0;
        }

        // --- Stage 5: Vocoder ---
        {
            auto infExp = createAndInit<Vo::VocoderInitArgs, Vo::VocoderRuntimeOptions>(
                m_stages.vocoder, "vocoder", singer);
            if (!infExp) {
                auto err = infExp.takeError();
                err.appendTrace(std::source_location::current(), "InferenceService::run");
                result.error = err;
                return result;
            }
            auto inference = infExp.take();

            auto input = NO<Vo::VocoderStartInput>::create();
            input->mel = mel;
            input->f0 = f0;

            auto resExp = startAndCheck<Vo::VocoderStartInput, Vo::VocoderResult>(
                inference, input, "vocoder", singer);
            if (!resExp) {
                auto err = resExp.takeError();
                err.appendTrace(std::source_location::current(), "InferenceService::run");
                result.error = err;
                return result;
            }

            const auto &audioData = resExp.value()->audioData;
            // Vocoder produces IEEE_FLOAT PCM bytes; copy into float vector.
            if (!audioData.empty()) {
                const auto sampleCount = audioData.size() / sizeof(float);
                result.audio.resize(sampleCount);
                if (sampleCount > 0) {
                    std::memcpy(result.audio.data(), audioData.data(),
                                sampleCount * sizeof(float));
                }
            }

            // Populate sampleRate from vocoder configuration.
            const auto vocoderConfig =
                m_stages.vocoder.spec->configuration().as<Vo::VocoderConfiguration>();
            if (vocoderConfig) {
                result.sampleRate = vocoderConfig->sampleRate;
            }
            result.channels = 1;
        }

        result.error = srt::core::Error();
        return result;
    }

} // namespace ds::infer
