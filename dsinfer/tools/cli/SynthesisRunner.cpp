#include "SynthesisRunner.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

#include <synthrt/Core/ContribSpecExtension.h>
#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Support/Logging.h>
#include <synthrt/SVS/SingerContrib.h>
#include <synthrt/SVS/SingerPipelineExecutive.h>

#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>
#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

#include <WavFile.h>

namespace ds::cli {

    namespace Common = Api::Common::L1;
    namespace Acoustic = Api::Acoustic::L1;
    namespace Duration = Api::Duration::L1;
    namespace Pitch = Api::Pitch::L1;
    namespace Variance = Api::Variance::L1;
    namespace Vocoder = Api::Vocoder::L1;
    namespace DiffSinger = Api::DiffSinger::L1;

    static srt::LogCategory s_cliLog("cli");

    namespace {

        struct AcousticOutput {
            std::shared_ptr<ITensor> mel;
            std::shared_ptr<ITensor> f0;
        };

        template <class T>
        T *requireInference(srt::Expected<T *> result, std::string_view role,
                            std::string_view singer) {
            if (!result) {
                throw std::runtime_error(
                    stdc::formatN(R"(failed to create %1 inference for singer "%2": %3)", role,
                                  singer, result.error().message()));
            }
            // The Singer Pipeline supervises the returned pointer. The caller does not own it.
            return *result;
        }

        srt::SingerSpec &findSinger(srt::PackageHandle &package, std::string_view singerId) {
            for (auto contribution : package.contributions("singer")) {
                if (contribution->locator().contributionId() == singerId) {
                    return *contribution->as<srt::SingerSpec>();
                }
            }
            throw std::runtime_error(
                stdc::formatN(R"(singer "%1" not found in package)", singerId));
        }

        std::unique_ptr<srt::SingerPipelineExecutive> createPipeline(srt::SingerSpec &singerSpec,
                                                                     std::string_view singerId) {
            if (singerSpec.interface() != DiffSinger::API_INTERFACE ||
                singerSpec.variant() != DiffSinger::API_VARIANT ||
                singerSpec.level() != DiffSinger::API_LEVEL) {
                throw std::runtime_error(stdc::formatN(
                    R"(singer "%1" does not implement the DiffSinger Level 1 contract)", singerId));
            }

            auto extension =
                srt::ContribSpecExtension::findFromSpec<DiffSinger::DiffSingerPipelineExecutive>(
                    singerSpec);
            if (!extension) {
                throw std::runtime_error(stdc::formatN(
                    R"(singer "%1" does not provide the DiffSinger synthesis pipeline)", singerId));
            }

            auto result = extension->as<srt::SingerPipelineExtension>()->createPipeline(
                DiffSinger::DiffSingerPipelineRuntimeOptions());
            if (!result) {
                throw std::runtime_error(
                    stdc::formatN(R"(failed to create the synthesis pipeline for singer "%1": %2)",
                                  singerId, result.error().message()));
            }
            return result.take();
        }

        void runDuration(Duration::DurationExecutive &inference,
                         Acoustic::AcousticStartInput &acousticInput, std::string_view singerId) {
            if (auto result = inference.initialize(Duration::DurationInitArgs()); !result) {
                throw std::runtime_error(
                    stdc::formatN(R"(failed to initialize duration inference for singer "%1": %2)",
                                  singerId, result.error().message()));
            }

            Duration::DurationStartInput input;
            input.duration = acousticInput.duration;
            input.words = acousticInput.words;

            auto result = inference.start(input);
            if (!result) {
                throw std::runtime_error(
                    stdc::formatN(R"(failed to start duration inference for singer "%1": %2)",
                                  singerId, result.error().message()));
            }

            size_t durationIndex = 0;
            for (auto &word : acousticInput.words) {
                double timeCursor = 0.0;
                for (auto &phoneme : word.phones) {
                    if (durationIndex >= (*result)->durations.size()) {
                        return;
                    }
                    phoneme.start = timeCursor;
                    timeCursor += (*result)->durations[durationIndex];
                    ++durationIndex;
                }
            }
        }

        void runPitch(Pitch::PitchExecutive &inference, Acoustic::AcousticStartInput &acousticInput,
                      std::string_view singerId) {
            if (auto result = inference.initialize(Pitch::PitchInitArgs()); !result) {
                throw std::runtime_error(
                    stdc::formatN(R"(failed to initialize pitch inference for singer "%1": %2)",
                                  singerId, result.error().message()));
            }

            Pitch::PitchStartInput input;
            input.duration = acousticInput.duration;
            input.words = acousticInput.words;
            for (const auto &parameter : acousticInput.parameters) {
                if (parameter.tag == Common::Tags::Pitch || parameter.tag == Common::Tags::Expr) {
                    input.parameters.push_back(
                        {parameter.tag, parameter.values, parameter.interval, parameter.retake});
                }
            }
            input.speakers = acousticInput.speakers;
            input.steps = acousticInput.steps;

            auto result = inference.start(input);
            if (!result) {
                throw std::runtime_error(
                    stdc::formatN(R"(failed to start pitch inference for singer "%1": %2)",
                                  singerId, result.error().message()));
            }

            bool hasPitch = false;
            for (auto &parameter : acousticInput.parameters) {
                if (parameter.tag == Common::Tags::Pitch) {
                    parameter.interval = (*result)->interval;
                    parameter.values = (*result)->pitch;
                    hasPitch = true;
                }
            }
            if (!hasPitch) {
                acousticInput.parameters.emplace_back(Common::InputParameterInfo{
                    Common::Tags::Pitch, (*result)->pitch, (*result)->interval});
            }
        }

        void runVariance(Variance::VarianceExecutive &inference,
                         Acoustic::AcousticStartInput &acousticInput, std::string_view singerId) {
            const auto schema = inference.spec().exports()->as<Variance::VarianceSchema>();
            if (auto result = inference.initialize(Variance::VarianceInitArgs()); !result) {
                throw std::runtime_error(
                    stdc::formatN(R"(failed to initialize variance inference for singer "%1": %2)",
                                  singerId, result.error().message()));
            }

            Variance::VarianceStartInput input;
            input.duration = acousticInput.duration;
            input.words = acousticInput.words;
            for (const auto &parameter : acousticInput.parameters) {
                if (parameter.tag == Common::Tags::Pitch) {
                    input.parameters.push_back({Common::Tags::Pitch, parameter.values,
                                                parameter.interval, parameter.retake});
                    continue;
                }

                if (std::find(schema->predictions.begin(), schema->predictions.end(),
                              parameter.tag) != schema->predictions.end()) {
                    input.parameters.push_back(
                        {parameter.tag, parameter.values, parameter.interval, parameter.retake});
                }
            }
            input.speakers = acousticInput.speakers;
            input.steps = acousticInput.steps;

            auto result = inference.start(input);
            if (!result) {
                throw std::runtime_error(
                    stdc::formatN(R"(failed to start variance inference for singer "%1": %2)",
                                  singerId, result.error().message()));
            }

            for (auto &predicted : (*result)->predictions) {
                auto original =
                    std::find_if(acousticInput.parameters.begin(), acousticInput.parameters.end(),
                                 [&](const Common::InputParameterInfo &parameter) {
                                     return parameter.tag == predicted.tag;
                                 });
                if (original != acousticInput.parameters.end()) {
                    original->interval = predicted.interval;
                    original->values = std::move(predicted.values);
                    original->retake = std::nullopt;
                    continue;
                }
                acousticInput.parameters.emplace_back(std::move(predicted));
            }
        }

        AcousticOutput runAcoustic(Acoustic::AcousticExecutive &inference,
                                   Acoustic::AcousticStartInput &input, std::string_view singerId) {
            if (auto result = inference.initialize(Acoustic::AcousticInitArgs()); !result) {
                throw std::runtime_error(
                    stdc::formatN(R"(failed to initialize acoustic inference for singer "%1": %2)",
                                  singerId, result.error().message()));
            }

            auto result = inference.start(input);
            if (!result) {
                throw std::runtime_error(
                    stdc::formatN(R"(failed to start acoustic inference for singer "%1": %2)",
                                  singerId, result.error().message()));
            }
            return {(*result)->mel, (*result)->f0};
        }

        std::vector<uint8_t> runVocoder(Vocoder::VocoderExecutive &inference,
                                        const AcousticOutput &acousticOutput,
                                        std::string_view singerId) {
            if (auto result = inference.initialize(Vocoder::VocoderInitArgs()); !result) {
                throw std::runtime_error(
                    stdc::formatN(R"(failed to initialize vocoder inference for singer "%1": %2)",
                                  singerId, result.error().message()));
            }

            Vocoder::VocoderStartInput input;
            input.mel = acousticOutput.mel;
            input.f0 = acousticOutput.f0;
            auto result = inference.start(input);
            if (!result) {
                throw std::runtime_error(
                    stdc::formatN(R"(failed to start vocoder inference for singer "%1": %2)",
                                  singerId, result.error().message()));
            }
            return std::move((*result)->audioData);
        }

        void writeWav(const std::filesystem::path &path, const std::vector<uint8_t> &audioData,
                      int sampleRate) {
            WavFile::DataFormat format{};
            format.container = WavFile::Container::RIFF;
            format.format = WavFile::WaveFormat::IEEE_FLOAT;
            format.channels = 1;
            format.sampleRate = sampleRate;
            format.bitsPerSample = 32;

            WavFile wav;
            if (!wav.init_file_write(path, format)) {
                throw std::runtime_error("failed to initialize WAV writer");
            }

            const auto frameCount = audioData.size() / (format.channels * sizeof(float));
            if (wav.write_pcm_frames(frameCount, audioData.data()) != frameCount) {
                throw std::runtime_error("failed to write all WAV frames");
            }
            wav.close();
            s_cliLog.srtSuccess("Saved audio to " + stdc::path::to_utf8(path));
        }

    }

    void SynthesisRunner::run(const std::filesystem::path &packagePath, SynthesisInput input,
                              const std::filesystem::path &outputPath) {
        // Dependencies are selected from the same installed Package collection as the root.
        m_synthUnit.setPackagePaths({packagePath.parent_path()});
        auto packageResult = m_synthUnit.openPackage(packagePath, srt::SynthUnit::Load);
        if (!packageResult) {
            throw std::runtime_error(stdc::formatN(R"(failed to open package "%1": %2)",
                                                   packagePath, packageResult.error().message()));
        }
        auto package = packageResult.take();
        auto &singerSpec = findSinger(package, input.singer);
        auto pipelineOwner = createPipeline(singerSpec, input.singer);
        auto pipeline = pipelineOwner->as<DiffSinger::DiffSingerPipelineExecutive>();

        // Create every required stage before model execution begins.
        auto duration = requireInference(
            pipeline->createDuration(Duration::DurationRuntimeOptions()), "duration", input.singer);
        auto pitch = requireInference(pipeline->createPitch(Pitch::PitchRuntimeOptions()), "pitch",
                                      input.singer);
        auto variance = requireInference(
            pipeline->createVariance(Variance::VarianceRuntimeOptions()), "variance", input.singer);
        auto acoustic = requireInference(
            pipeline->createAcoustic(Acoustic::AcousticRuntimeOptions()), "acoustic", input.singer);
        auto vocoder = requireInference(pipeline->createVocoder(Vocoder::VocoderRuntimeOptions()),
                                        "vocoder", input.singer);

        const auto vocoderConfiguration =
            vocoder->spec().configuration()->as<Vocoder::VocoderConfiguration>();
        runDuration(*duration, *input.acoustic, input.singer);
        runPitch(*pitch, *input.acoustic, input.singer);
        runVariance(*variance, *input.acoustic, input.singer);
        const auto acousticOutput = runAcoustic(*acoustic, *input.acoustic, input.singer);
        const auto audioData = runVocoder(*vocoder, acousticOutput, input.singer);
        writeWav(outputPath, audioData, vocoderConfiguration->sampleRate);
    }

}
