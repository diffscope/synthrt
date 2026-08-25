#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include <stdcorelib/adt/vlarray.h>
#include <stdcorelib/console.h>
#include <stdcorelib/path.h>
#include <stdcorelib/str.h>
#include <stdcorelib/support/commandline.h>
#include <stdcorelib/system.h>

#include <synthrt/Core/SynthUnit.h>
#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Support/JSON.h>
#include <synthrt/Support/Logging.h>
#include <synthrt/SVS/SingerContrib.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/InferenceExecInstance.h>

#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceDriverFactory.h>
#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>
#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>
#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

#include <AcousticInputParser.h>
#include <WavFile.h>

namespace fs = std::filesystem;

namespace Co = ds::Api::Common::L1;
namespace Ac = ds::Api::Acoustic::L1;
namespace Dur = ds::Api::Duration::L1;
namespace Pit = ds::Api::Pitch::L1;
namespace Var = ds::Api::Variance::L1;
namespace Vo = ds::Api::Vocoder::L1;
namespace DiffSinger = ds::Api::DiffSinger::L1;

using EP = ds::Api::Onnx::ExecutionProvider;

static srt::LogCategory cliLog("cli");

static void logReportCallback(int level, const srt::LogContext &ctx, const std::string_view &msg) {
    using namespace srt;
    using namespace stdc;

    // Keep the runner output focused on progress, warnings, and failures.
    if (level < Logger::Success) {
        return;
    }

    auto t = std::time(nullptr);
    auto tm = std::localtime(&t);

    std::stringstream ss;
    ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
    auto dts = ss.str();

    int foreground, background;
    switch (level) {
        case Logger::Success:
            foreground = console::lightgreen;
            background = foreground;
            break;
        case Logger::Warning:
            foreground = console::yellow;
            background = foreground;
            break;
        case Logger::Critical:
        case Logger::Fatal:
            foreground = console::red;
            background = foreground;
            break;
        default:
            foreground = console::nocolor;
            background = console::white;
            break;
    }

    const char *sig;
    switch (level) {
        case Logger::Trace:
            sig = "T";
            break;
        case Logger::Debug:
            sig = "D";
            break;
        case Logger::Success:
            sig = "S";
            break;
        case Logger::Warning:
            sig = "W";
            break;
        case Logger::Critical:
            sig = "C";
            break;
        case Logger::Fatal:
            sig = "F";
            break;
        default:
            sig = "I";
            break;
    }
    console::printf(console::nostyle, foreground, console::nocolor, "[%s] %-15s", dts.c_str(),
                    ctx.category);
    console::printf(console::nostyle, console::nocolor, background, " %s ", sig);
    console::printf(console::nostyle, console::nocolor, console::nocolor, "  ");
    console::println(console::nostyle, foreground, console::nocolor, msg);
}

static fs::path defaultPluginRoot() {
    // The build and install layouts both place bin and lib beside each other.
    return stdc::system::application_directory().parent_path() / STDC_TSTR("lib") /
           STDC_TSTR("plugins") / STDC_TSTR("dsinfer");
}

static void initializeSynthUnit(srt::SynthUnit &su, ds::InferenceDriverFactory &driverFactory,
                                const fs::path &pluginRoot, EP ep, int deviceIndex) {
    // Each contribution category discovers only plugins from its own directory.
    su.setPluginPaths("singer", {pluginRoot / STDC_TSTR("singerproviders")});
    su.setPluginPaths("inference", {pluginRoot / STDC_TSTR("inferenceinterpreters")});

    // Drivers are runtime services rather than contributions, so they use a separate factory.
    driverFactory.addPluginPath(pluginRoot / STDC_TSTR("inferencedrivers"));
    auto driverResult = driverFactory.create(ds::Api::Onnx::API_NAME);
    if (!driverResult) {
        throw std::runtime_error("failed to load inference driver: " +
                                 driverResult.error().message());
    }
    auto onnxDriver = driverResult.take();
    ds::Api::Onnx::DriverInitArgs onnxArgs;

    onnxArgs.ep = ep;
    auto ortParentPath = pluginRoot / STDC_TSTR("inferencedrivers") /
                         stdc::path::from_utf8(ds::Api::Onnx::API_NAME) / STDC_TSTR("runtimes") /
                         STDC_TSTR("onnx");
    if (ep == EP::CUDA) {
        onnxArgs.runtimePath = ortParentPath / STDC_TSTR("cuda");
    } else {
        onnxArgs.runtimePath = ortParentPath / STDC_TSTR("default");
    }
    onnxArgs.deviceIndex = deviceIndex;

    if (auto exp = onnxDriver->initialize(onnxArgs); !exp) {
        throw std::runtime_error(
            stdc::formatN(R"(failed to initialize onnx driver: %1)", exp.error().message()));
    }

    // SynthUnit owns the initialized driver for as long as loaded contributions can use it.
    if (auto result = su.addRuntimeService(std::move(onnxDriver)); !result) {
        throw std::runtime_error("failed to register inference driver: " +
                                 result.error().message());
    }
}

// The input file selects a singer and carries the initial acoustic request shared by all stages.
// It has the following JSON structure. Every field except singer is optional.
//
// {
//   "singer": "singer-contribution-id",
//   "duration": 1.5,
//   "steps": 20,
//   "depth": 0.8,
//   "words": [
//     {
//       "phones": [
//         {
//           "token": "n",
//           "language": "zh",
//           "tone": 2,
//           "start": 0.0,
//           "speakers": [
//             {"name": "main", "proportion": 1.0}
//           ]
//         }
//       ],
//       "notes": [
//         {
//           "key": "C4+0",
//           "cents": 0,
//           "duration": 1.5,
//           "glide": "none",
//           "is_rest": false
//         }
//       ]
//     }
//   ],
//   "parameters": [
//     {
//       "tag": "pitch",
//       "dynamic": true,
//       "values": [60.0, 60.1, 60.2],
//       "interval": 0.005,
//       "retake": {"start": 0.2, "end": 0.8}
//     },
//     {
//       "tag": "energy",
//       "dynamic": false,
//       "value": 1.0
//     }
//   ],
//   "speakers": [
//     {
//       "name": "main",
//       "dynamic": true,
//       "values": [1.0, 1.0, 1.0],
//       "interval": 0.005
//     }
//   ]
// }
//
// singer must be a nonempty contribution identifier. duration and depth are floating point
// numbers. steps is converted to an integer.
//
// words contains objects with optional phones and notes arrays. A phone may provide token,
// language, tone, start, and speakers. Each phone speaker requires name and defaults proportion
// to 1. A note key may be a MIDI integer, a fractional MIDI number, a string such as C4+0 or
// D#4-25, or REST. cents is added to the value encoded by key. glide accepts up, down, or none.
//
// parameters accepts pitch, expr, f0, tone_shift, energy, breathiness, voicing, tension,
// mouth_opening, gender, and velocity. Unknown tags are ignored. A curve with dynamic set to true
// requires a numeric values array and a positive interval. A constant curve omits dynamic or sets
// it to false and requires one numeric value. retake optionally selects a range with numeric start
// and end values.
//
// Top level speakers use the same constant or dynamic curve representation as parameters. Each
// speaker also requires name. Unknown object fields are preserved by the JSON parser but ignored
// by the input conversion.
struct InputObject {
    std::string singer;
    std::unique_ptr<Ac::AcousticStartInput> input;

    static srt::Expected<InputObject> load(const fs::path &path) {
        std::ifstream ifs(path);
        if (!ifs) {
            return srt::Error(srt::Error::FileNotOpen,
                              stdc::formatN(R"(failed to open input file "%1")", path));
        }
        std::string jsonStr((std::istreambuf_iterator<char>(ifs)),
                            (std::istreambuf_iterator<char>()));

        stdc::json::ParseError jsonError;
        srt::JsonValue jsonDoc = srt::JsonValue::fromJson(jsonStr, true, &jsonError);
        if (jsonError) {
            return srt::Error(srt::Error::InvalidFormat, std::move(jsonError.what));
        }
        if (!jsonDoc.isObject()) {
            return srt::Error(srt::Error::InvalidFormat, "not an object");
        }
        const auto &docObj = jsonDoc.toObject();
        InputObject res;
        {
            auto it = docObj.find("singer");
            if (it == docObj.end()) {
                return srt::Error(srt::Error::InvalidFormat, "missing singer field");
            }
            if (!it->second.isString()) {
                return srt::Error(srt::Error::InvalidFormat, "singer field type mismatch");
            }
            res.singer = it->second.toString();
            if (res.singer.empty()) {
                return srt::Error(srt::Error::InvalidFormat, "empty singer field");
            }

            if (auto exp = ds::parseAcousticStartInput(docObj); exp) {
                res.input = exp.take();
            } else {
                return exp.takeError();
            }
        }
        return res;
    }
};

template <class T>
static T *requireInference(srt::Expected<T *> result, std::string_view role,
                           std::string_view singer) {
    if (!result) {
        throw std::runtime_error(
            stdc::formatN(R"(failed to create %1 inference for singer "%2": %3)", role, singer,
                          result.error().message()));
    }
    // The returned pointer is supervised by the Singer Pipeline and is not owned by the caller.
    return *result;
}

static int execute(const fs::path &packagePath, const fs::path &inputPath,
                   const fs::path &outputWavPath, const fs::path &pluginRoot, EP ep,
                   int deviceIndex) {
    // Read input
    InputObject input;
    if (auto exp = InputObject::load(inputPath); !exp) {
        const auto &err = exp.error();
        throw std::runtime_error(
            stdc::formatN(R"(failed to read input file "%1": %2)", inputPath, err.message()));
    } else {
        input = exp.take();
    }

    ds::InferenceDriverFactory driverFactory;
    srt::SynthUnit su;
    initializeSynthUnit(su, driverFactory, pluginRoot, ep, deviceIndex);

    // Dependencies are selected from the same installed Package collection as the root Package.
    su.setPackagePaths({packagePath.parent_path()});

    // Load package
    srt::PackageHandle pkg;
    if (auto exp = su.openPackage(packagePath, srt::SynthUnit::Load); !exp) {
        throw std::runtime_error(stdc::formatN(R"(failed to open package "%1": %2)", packagePath,
                                               exp.error().message()));
    } else {
        pkg = exp.take();
    }

    // Contribution identifiers are unique only within their category.
    srt::SingerSpec *singerSpec = nullptr;
    for (auto singer : pkg.contributions("singer")) {
        if (singer->locator().contributionId() == input.singer) {
            singerSpec = singer->as<srt::SingerSpec>();
            break;
        }
    }
    if (!singerSpec) {
        throw std::runtime_error(
            stdc::formatN(R"(singer "%1" not found in package)", input.singer));
    }

    // Verify the contract identity before using the unchecked typed cast below.
    if (singerSpec->interface() != DiffSinger::API_INTERFACE ||
        singerSpec->variant() != DiffSinger::API_VARIANT ||
        singerSpec->level() != DiffSinger::API_LEVEL) {
        throw std::runtime_error(stdc::formatN(
            R"(singer "%1" does not implement the DiffSinger Level 1 contract)", input.singer));
    }

    // The root Pipeline owns every inference instance created through its Import roles.
    std::unique_ptr<srt::SingerPipelineExecInstance> pipelineOwner;
    if (auto result = singerSpec->createPipeline(DiffSinger::DiffSingerPipelineRuntimeOptions());
        !result) {
        throw std::runtime_error(
            stdc::formatN(R"(failed to create the synthesis pipeline for singer "%1": %2)",
                          input.singer, result.error().message()));
    } else {
        pipelineOwner = result.take();
    }
    auto pipeline = pipelineOwner->as<DiffSinger::DiffSingerPipelineExecInstance>();

    // Create every required stage before any model starts running. This exposes an incomplete
    // singer declaration without leaving a partially executed synthesis request.
    auto durationInference = requireInference(
        pipeline->createDuration(Dur::DurationRuntimeOptions()), "duration", input.singer);
    auto pitchInference =
        requireInference(pipeline->createPitch(Pit::PitchRuntimeOptions()), "pitch", input.singer);
    auto varianceInference = requireInference(
        pipeline->createVariance(Var::VarianceRuntimeOptions()), "variance", input.singer);
    auto acousticInference = requireInference(
        pipeline->createAcoustic(Ac::AcousticRuntimeOptions()), "acoustic", input.singer);
    auto vocoderInference = requireInference(pipeline->createVocoder(Vo::VocoderRuntimeOptions()),
                                             "vocoder", input.singer);

    // Keep this local check until InferenceSpec exposes contract specific compatibility checks.
    const auto acousticConfig =
        acousticInference->spec().configuration()->as<Ac::AcousticConfiguration>();
    const auto vocoderConfig =
        vocoderInference->spec().configuration()->as<Vo::VocoderConfiguration>();
    stdc::vlarray<std::string> unmatchedFields;
    if (acousticConfig->sampleRate != vocoderConfig->sampleRate) {
        unmatchedFields.emplace_back("sampleRate");
    }
    if (acousticConfig->hopSize != vocoderConfig->hopSize) {
        unmatchedFields.emplace_back("hopSize");
    }
    if (acousticConfig->winSize != vocoderConfig->winSize) {
        unmatchedFields.emplace_back("winSize");
    }
    if (acousticConfig->fftSize != vocoderConfig->fftSize) {
        unmatchedFields.emplace_back("fftSize");
    }
    if (acousticConfig->melChannels != vocoderConfig->melChannels) {
        unmatchedFields.emplace_back("melChannels");
    }
    if (acousticConfig->melMinFreq != vocoderConfig->melMinFreq) {
        unmatchedFields.emplace_back("melMinFreq");
    }
    if (acousticConfig->melMaxFreq != vocoderConfig->melMaxFreq) {
        unmatchedFields.emplace_back("melMaxFreq");
    }
    if (acousticConfig->melBase != vocoderConfig->melBase) {
        unmatchedFields.emplace_back("melBase");
    }
    if (acousticConfig->melScale != vocoderConfig->melScale) {
        unmatchedFields.emplace_back("melScale");
    }
    if (!unmatchedFields.empty()) {
        throw std::runtime_error(stdc::formatN("acoustic and vocoder config mismatch: %1",
                                               stdc::join(unmatchedFields, ", ")));
    }

    // Duration establishes phoneme timing consumed by every following stage.
    {
        auto inference = durationInference;
        if (auto exp = inference->initialize(Dur::DurationInitArgs()); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize duration inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        }

        Dur::DurationStartInput durationInput;
        // The duration contract consumes only score structure and its total duration.
        durationInput.duration = input.input->duration;
        durationInput.words = input.input->words;

        // Start inference
        std::unique_ptr<Dur::DurationResult> resultOwner;
        if (auto exp = inference->start(durationInput); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to start duration inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            resultOwner = exp.take();
        }
        auto result = resultOwner.get();

        // Feed predicted phoneme timing back into the shared acoustic request.
        auto updatePhonemeStarts = [](std::vector<Co::InputWordInfo> &words,
                                      const std::vector<double> &phonemeDurations) {
            size_t i = 0;
            for (auto &word : words) {
                double timeCursor = 0.0;
                for (auto &phoneme : word.phones) {
                    if (i >= phonemeDurations.size()) {
                        return;
                    }
                    phoneme.start = timeCursor;
                    timeCursor += phonemeDurations[i];
                    ++i;
                }
            }
        };

        updatePhonemeStarts(input.input->words, result->durations);
    }

    // Pitch predicts or refines the pitch curve used by variance and acoustic inference.
    {
        auto inference = pitchInference;
        if (auto exp = inference->initialize(Pit::PitchInitArgs()); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize pitch inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        }

        Pit::PitchStartInput pitchInput;
        // Only controls declared by the pitch contract are forwarded to this stage.
        pitchInput.duration = input.input->duration;
        pitchInput.words = input.input->words;
        for (const auto &param : input.input->parameters) {
            if (param.tag == Co::Tags::Pitch) {
                pitchInput.parameters.push_back(
                    {Co::Tags::Pitch, param.values, param.interval, param.retake});
            } else if (param.tag == Co::Tags::Expr) {
                pitchInput.parameters.push_back(
                    {Co::Tags::Expr, param.values, param.interval, param.retake});
            }
        }
        pitchInput.speakers = input.input->speakers;
        pitchInput.steps = input.input->steps;

        // Start inference
        std::unique_ptr<Pit::PitchResult> resultOwner;
        if (auto exp = inference->start(pitchInput); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to start pitch inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            resultOwner = exp.take();
        }
        auto result = resultOwner.get();

        // Replace the existing pitch control or append it when the input omitted one.
        auto res = result->pitch;
        auto interval = result->interval;
        bool hasPitch = false;
        for (auto &param : input.input->parameters) {
            if (param.tag == Co::Tags::Pitch) {
                param.interval = interval;
                param.values = res;
                hasPitch = true;
            }
        }
        if (!hasPitch) {
            input.input->parameters.emplace_back(
                Co::InputParameterInfo{Co::Tags::Pitch, res, interval});
        }
    }

    // Variance fills the model specific control curves exported by its schema.
    {
        auto inference = varianceInference;
        const auto schema = inference->spec().exports()->as<Var::VarianceSchema>();
        if (auto exp = inference->initialize(Var::VarianceInitArgs()); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize variance inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        }

        Var::VarianceStartInput varianceInput;
        // Preserve pitch and forward only variance controls that this model predicts.
        varianceInput.duration = input.input->duration;
        varianceInput.words = input.input->words;
        for (const auto &param : input.input->parameters) {
            if (param.tag == Co::Tags::Pitch) {
                varianceInput.parameters.push_back(
                    {Co::Tags::Pitch, param.values, param.interval, param.retake});
                continue;
            }

            for (const auto &prediction : schema->predictions) {
                if (prediction == param.tag) {
                    varianceInput.parameters.push_back(
                        {prediction, param.values, param.interval, param.retake});
                }
            }
        }
        varianceInput.speakers = input.input->speakers;
        varianceInput.steps = input.input->steps;

        // Start inference
        std::unique_ptr<Var::VarianceResult> resultOwner;
        if (auto exp = inference->start(varianceInput); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to start variance inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            resultOwner = exp.take();
        }
        auto result = resultOwner.get();

        // Merge predictions by ParamTag so input ordering has no semantic effect.
        for (auto &predicted : result->predictions) {
            auto original =
                std::find_if(input.input->parameters.begin(), input.input->parameters.end(),
                             [&](const Co::InputParameterInfo &parameter) {
                                 return parameter.tag == predicted.tag;
                             });
            if (original != input.input->parameters.end()) {
                original->interval = predicted.interval;
                original->values = std::move(predicted.values);
                original->retake = std::nullopt;
                continue;
            }
            input.input->parameters.emplace_back(std::move(predicted));
        }
    }

    // Acoustic inference turns the completed score and control curves into feature tensors.
    std::shared_ptr<ds::ITensor> mel;
    std::shared_ptr<ds::ITensor> f0;
    {
        auto inference = acousticInference;
        if (auto exp = inference->initialize(Ac::AcousticInitArgs()); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize acoustic inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        }

        // Start inference
        std::unique_ptr<Ac::AcousticResult> resultOwner;
        if (auto exp = inference->start(*input.input); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to start acoustic inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            resultOwner = exp.take();
        }
        auto result = resultOwner.get();
        mel = result->mel;
        f0 = result->f0;
    }

    // The tensor objects bridge the acoustic and vocoder contracts without copying their data.
    std::vector<uint8_t> audioData;
    {
        auto inference = vocoderInference;
        if (auto exp = inference->initialize(Vo::VocoderInitArgs()); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize vocoder inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        }

        Vo::VocoderStartInput vocoderInput;
        vocoderInput.mel = mel;
        vocoderInput.f0 = f0;

        // Start inference
        std::unique_ptr<Vo::VocoderResult> resultOwner;
        if (auto exp = inference->start(vocoderInput); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to start vocoder inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            resultOwner = exp.take();
        }
        auto result = resultOwner.get();
        audioData = std::move(result->audioData);
    }

    // Vocoder output is contiguous native float data ready for the WAV payload.
    {
        using ds::WavFile;

        WavFile::DataFormat format{};
        format.container = WavFile::Container::RIFF;
        format.format = WavFile::WaveFormat::IEEE_FLOAT;
        format.channels = 1;
        format.sampleRate = vocoderConfig->sampleRate;
        format.bitsPerSample = 32;

        WavFile wav;
        if (!wav.init_file_write(outputWavPath, format)) {
            throw std::runtime_error("failed to initialize WAV writer");
        }

        auto totalPCMFrameCount = audioData.size() / (format.channels * sizeof(float));

        auto framesWritten = wav.write_pcm_frames(totalPCMFrameCount, audioData.data());
        if (framesWritten != totalPCMFrameCount) {
            throw std::runtime_error("failed to write all WAV frames");
        }
        wav.close();

        cliLog.srtSuccess("Saved audio to " + stdc::path::to_utf8(outputWavPath));
    }

    return 0;
}

static std::string exceptionMessage(const std::exception &e) {
    std::string msg = e.what();
#ifdef _WIN32
    // MSVC reports filesystem paths in the active ANSI code page rather than UTF 8.
    if (typeid(e) == typeid(fs::filesystem_error)) {
        auto &err = static_cast<const fs::filesystem_error &>(e);
        msg = stdc::wstring_conv::to_utf8(stdc::wstring_conv::from_ansi(err.what()));
    }
#endif
    return msg;
}

static EP parseExecutionProvider(std::string value) {
    value = stdc::to_lower(std::move(value));
    if (value == "dml" || value == "directml") {
        return EP::DML;
    }
    if (value == "cuda") {
        return EP::CUDA;
    }
    if (value == "coreml") {
        return EP::CoreML;
    }
    return EP::CPU;
}

static int runCommand(const stdc::cli::ParseResult &result) {
    const auto packagePath = stdc::path::from_utf8(*result.value<std::string>(0));
    const auto inputPath = stdc::path::from_utf8(*result.value<std::string>(1));
    const auto outputWavPath = stdc::path::from_utf8(*result.value<std::string>(2));
    auto pluginRoot = defaultPluginRoot();
    if (auto value = result.valueForOption<std::string>("--plugin-root")) {
        pluginRoot = stdc::path::from_utf8(*value);
    }
    const auto ep =
        parseExecutionProvider(result.valueForOption<std::string>("--ep").value_or("cpu"));
    const auto deviceIndex = result.valueForOption<int>("--device").value_or(0);
    if (deviceIndex < 0) {
        stdc::console::critical("Error: device index cannot be negative");
        return 1;
    }

    try {
        return execute(packagePath, inputPath, outputWavPath, pluginRoot, ep, deviceIndex);
    } catch (const std::exception &e) {
        stdc::console::critical("Error: %1", exceptionMessage(e));
        return 1;
    }
}

static stdc::cli::Parser createCommandLineParser() {
    // stdc::cli validates required paths, option values, and integer syntax before the handler.
    stdc::cli::Command command(stdc::system::application_name(),
                               "Run a DiffSinger synthesis package from an acoustic input file.");
    command.addArgument(stdc::cli::Argument("package", "Installed Package directory."))
        .addArgument(stdc::cli::Argument("input", "Acoustic input JSON file."))
        .addArgument(stdc::cli::Argument("output", "Destination WAV file."))
        .addOption(
            stdc::cli::Option({"-e", "--ep"}, "ONNX Runtime execution provider. Defaults to cpu.")
                .arg(stdc::cli::Argument("provider")
                         .expect({"cpu", "dml", "directml", "cuda", "coreml"})))
        .addOption(
            stdc::cli::Option({"-d", "--device"}, "Execution provider device index. Defaults to 0.")
                .arg(stdc::cli::Argument("index").type<int>()))
        .addOption(
            stdc::cli::Option("--plugin-root", "Directory containing dsinfer plugin categories.")
                .arg("directory"))
        .addVersionOption(TOOL_VERSION)
        .addHelpOption(true)
        .setHandler(runCommand);

    stdc::cli::Parser parser(std::move(command));
    parser.setDisplayOptions(stdc::cli::Parser::ShowArgumentExpectedValues);
    return parser;
}

int main(int, char *[]) {
    srt::Logger::setLogCallback(logReportCallback);
    auto parser = createCommandLineParser();
    return parser.invoke(stdc::system::command_line_arguments(), 1);
}
