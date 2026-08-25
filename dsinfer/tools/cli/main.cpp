#include <filesystem>
#include <fstream>

#include <stdcorelib/system.h>
#include <stdcorelib/console.h>
#include <stdcorelib/path.h>

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

#include <AcousticInputParser.h>
#include <WavFile.h>

namespace fs = std::filesystem;

namespace Co = ds::Api::Common::L1;
namespace Ac = ds::Api::Acoustic::L1;
namespace Dur = ds::Api::Duration::L1;
namespace Pit = ds::Api::Pitch::L1;
namespace Var = ds::Api::Variance::L1;
namespace Vo = ds::Api::Vocoder::L1;

using EP = ds::Api::Onnx::ExecutionProvider;

static srt::LogCategory cliLog("cli");

static void log_report_callback(int level, const srt::LogContext &ctx,
                                const std::string_view &msg) {
    using namespace srt;
    using namespace stdc;

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

static void initializeSU(srt::SynthUnit &su, ds::InferenceDriverFactory &driverFactory, EP ep,
                         int deviceIndex) {
    // Get basic directories
    auto appDir = stdc::system::application_directory();
    auto defaultPluginDir =
        appDir.parent_path() / STDC_TSTR("lib") / STDC_TSTR("plugins") / STDC_TSTR("dsinfer");

    // Set default plugin directories
    su.setPluginPaths("singer", {defaultPluginDir / STDC_TSTR("singerproviders")});
    su.setPluginPaths("inference", {defaultPluginDir / STDC_TSTR("inferenceinterpreters")});

    // Load driver
    driverFactory.addPluginPath(defaultPluginDir / STDC_TSTR("inferencedrivers"));
    auto driverResult = driverFactory.create(ds::Api::Onnx::API_NAME);
    if (!driverResult) {
        throw std::runtime_error("failed to load inference driver: " +
                                 driverResult.error().message());
    }
    auto onnxDriver = driverResult.take();
    ds::Api::Onnx::DriverInitArgs onnxArgs;

    // TODO: users should be able to configure these args
    onnxArgs.ep = ep;
    auto ortParentPath = defaultPluginDir / STDC_TSTR("inferencedrivers") /
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

    if (auto result = su.addRuntimeService(std::move(onnxDriver)); !result) {
        throw std::runtime_error("failed to register inference driver: " +
                                 result.error().message());
    }
}

struct InputObject {
    std::string singer;
    std::unique_ptr<Ac::AcousticStartInput> input;

    static srt::Expected<InputObject> load(const fs::path &path) {
        // read all from path to string
        std::ifstream ifs(path);
        if (!ifs) {
            return srt::Error(srt::Error::FileNotOpen,
                              stdc::formatN(R"(failed to open input file "%1")", path));
        }
        std::string jsonStr((std::istreambuf_iterator<char>(ifs)),
                            (std::istreambuf_iterator<char>()));

        // parse JSON
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
            res.singer = it->second.toString();
            if (res.singer.empty()) {
                return srt::Error(srt::Error::InvalidFormat, "empty singer field");
            }

            // parse acoustic input
            if (auto exp = ds::parseAcousticStartInput(docObj); exp) {
                res.input = exp.take();
            } else {
                return exp.takeError();
            }
        }
        return res;
    }
};

static int exec(const fs::path &packagePath, const fs::path &inputPath,
                const fs::path &outputWavPath, EP ep, int deviceIndex) {
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
    initializeSU(su, driverFactory, ep, deviceIndex);

    // Add package directory to search path
    su.setPackagePaths({packagePath.parent_path()});

    // Load package
    srt::PackageHandle pkg;
    if (auto exp = su.openPackage(packagePath, srt::SynthUnit::Load); !exp) {
        throw std::runtime_error(stdc::formatN(R"(failed to open package "%1": %2)", packagePath,
                                               exp.error().message()));
    } else {
        pkg = exp.take();
    }
    // Find singer
    srt::SingerSpec *singerSpec = nullptr;
    for (auto *singer : pkg.contributions("singer")) {
        if (singer->locator().contributionId() == input.singer) {
            singerSpec = singer->as<srt::SingerSpec>();
            break;
        }
    }
    if (!singerSpec) {
        throw std::runtime_error(
            stdc::formatN(R"(singer "%1" not found in package)", input.singer));
    }

    struct ImportData {
        const srt::ContribImportOptions *options = nullptr;
        srt::InferenceSpec *inference = nullptr;
    };

    ImportData importDuration, importPitch, importVariance, importAcoustic, importVocoder;

    struct ImportEntry {
        std::string_view interface;
        ImportData *data;
    };

    ImportEntry imports[] = {
        {Dur::API_INTERFACE, &importDuration},
        {Pit::API_INTERFACE, &importPitch   },
        {Var::API_INTERFACE, &importVariance},
        {Ac::API_INTERFACE,  &importAcoustic},
        {Vo::API_INTERFACE,  &importVocoder },
    };

    // Assign imports
    for (const auto &imp : singerSpec->imports()) {
        auto *target = pkg.resolve(imp.locator());
        if (!target || target->locator().category() != "inference") {
            continue;
        }
        for (auto &entry : imports) {
            if (target->interface() == entry.interface) {
                *entry.data = {imp.options(), target->as<srt::InferenceSpec>()};
                break;
            }
        }
    }

    // Check for missing inferences
    for (const auto &entry : imports) {
        if (!entry.data->inference) {
            throw std::runtime_error(stdc::formatN(R"(%1 inference not found for singer "%2")",
                                                   entry.interface, input.singer));
        }
    }

    // Check whether acoustic and vocoder config match
    const auto acousticConfig =
        importAcoustic.inference->configuration()->as<Ac::AcousticConfiguration>();
    const auto vocoderConfig =
        importVocoder.inference->configuration()->as<Vo::VocoderConfiguration>();
    std::vector<std::string> unmatchedFields;
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

    // Run duration
    {
        std::unique_ptr<srt::InferenceExecInstance> inference;
        if (auto exp = importDuration.inference->createInference(*importDuration.options,
                                                                 Dur::DurationRuntimeOptions());
            !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to create duration inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            inference = exp.take();
        }
        if (auto exp = inference->initialize(Dur::DurationInitArgs()); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize duration inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        }

        auto durationInput = std::make_unique<Dur::DurationStartInput>();
        // Copy user inputs into duration model inputs
        durationInput->duration = input.input->duration;
        durationInput->words = input.input->words;

        // Start inference
        std::unique_ptr<srt::TaskResult> resultOwner;
        Dur::DurationResult *result = nullptr;
        if (auto exp = inference->start(*durationInput); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to start duration inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            resultOwner = exp.take();
            result = resultOwner->as<Dur::DurationResult>();
        }

        // Update user inputs in-place with duration model outputs
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

    // Run pitch
    {
        std::unique_ptr<srt::InferenceExecInstance> inference;
        if (auto exp = importPitch.inference->createInference(*importPitch.options,
                                                              Pit::PitchRuntimeOptions());
            !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to create pitch inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            inference = exp.take();
        }
        if (auto exp = inference->initialize(Pit::PitchInitArgs()); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize pitch inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        }

        auto pitchInput = std::make_unique<Pit::PitchStartInput>();
        // Copy user inputs into pitch model inputs
        pitchInput->duration = input.input->duration;
        pitchInput->words = input.input->words;
        for (const auto &param : input.input->parameters) {
            if (param.tag == Co::Tags::Pitch) {
                pitchInput->parameters.push_back(
                    {Co::Tags::Pitch, param.values, param.interval, param.retake});
            } else if (param.tag == Co::Tags::Expr) {
                pitchInput->parameters.push_back(
                    {Co::Tags::Expr, param.values, param.interval, param.retake});
            }
        }
        pitchInput->speakers = input.input->speakers;
        pitchInput->steps = input.input->steps;

        // Start inference
        std::unique_ptr<srt::TaskResult> resultOwner;
        Pit::PitchResult *result = nullptr;
        if (auto exp = inference->start(*pitchInput); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to start pitch inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            resultOwner = exp.take();
            result = resultOwner->as<Pit::PitchResult>();
        }

        // Update user inputs in-place with pitch model outputs
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

    // Run variance
    {
        std::unique_ptr<srt::InferenceExecInstance> inference;
        const auto schema = importVariance.inference->exports()->as<Var::VarianceSchema>();
        if (auto exp = importVariance.inference->createInference(*importVariance.options,
                                                                 Var::VarianceRuntimeOptions());
            !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to create variance inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            inference = exp.take();
        }
        if (auto exp = inference->initialize(Var::VarianceInitArgs()); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize variance inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        }

        auto varianceInput = std::make_unique<Var::VarianceStartInput>();
        // Copy user inputs into variance model inputs
        varianceInput->duration = input.input->duration;
        varianceInput->words = input.input->words;
        for (const auto &param : input.input->parameters) {
            if (param.tag == Co::Tags::Pitch) {
                varianceInput->parameters.push_back(
                    {Co::Tags::Pitch, param.values, param.interval, param.retake});
                continue;
            }

            for (const auto &prediction : schema->predictions) {
                if (prediction == param.tag) {
                    varianceInput->parameters.push_back(
                        {prediction, param.values, param.interval, param.retake});
                }
            }
        }
        varianceInput->speakers = input.input->speakers;
        varianceInput->steps = input.input->steps;

        // Start inference
        std::unique_ptr<srt::TaskResult> resultOwner;
        Var::VarianceResult *result = nullptr;
        if (auto exp = inference->start(*varianceInput); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to start variance inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            resultOwner = exp.take();
            result = resultOwner->as<Var::VarianceResult>();
        }

        // Update user inputs in-place with variance model outputs
        const auto nParams = schema->predictions.size();
        std::vector<char> satisfyParams(nParams, false);
        // schema->predictions.size() == result->predictions.size()
        // guaranteed if inference is successful
        for (size_t i = 0; i < nParams; i++) {
            auto &originalParam = input.input->parameters[i];
            for (auto &predicted : result->predictions) {
                if (originalParam.tag == predicted.tag) {
                    originalParam.interval = predicted.interval;
                    originalParam.values = std::move(predicted.values);
                    originalParam.retake = std::nullopt;
                    satisfyParams[i] = true;
                    break;
                }
            }
        }
        for (size_t i = 0; i < nParams; i++) {
            if (satisfyParams[i]) {
                continue;
            }
            for (auto &predictedParam : result->predictions) {
                if (predictedParam.tag == schema->predictions[i]) {
                    input.input->parameters.emplace_back(std::move(predictedParam));
                    break;
                }
            }
        }
    }

    // Run acoustic
    std::shared_ptr<ds::ITensor> mel;
    std::shared_ptr<ds::ITensor> f0;
    {
        // Prepare
        std::unique_ptr<srt::InferenceExecInstance> inference;
        if (auto exp = importAcoustic.inference->createInference(*importAcoustic.options,
                                                                 Ac::AcousticRuntimeOptions());
            !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to create acoustic inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            inference = exp.take();
        }
        if (auto exp = inference->initialize(Ac::AcousticInitArgs()); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize acoustic inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        }

        // Start inference
        std::unique_ptr<srt::TaskResult> resultOwner;
        Ac::AcousticResult *result = nullptr;
        if (auto exp = inference->start(*input.input); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to start acoustic inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            resultOwner = exp.take();
            result = resultOwner->as<Ac::AcousticResult>();
        }
        mel = result->mel;
        f0 = result->f0;
    }

    // Run vocoder
    std::vector<uint8_t> audioData;
    {
        // Prepare
        std::unique_ptr<srt::InferenceExecInstance> inference;
        if (auto exp = importVocoder.inference->createInference(*importVocoder.options,
                                                                Vo::VocoderRuntimeOptions());
            !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to create vocoder inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            inference = exp.take();
        }
        if (auto exp = inference->initialize(Vo::VocoderInitArgs()); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize vocoder inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        }

        auto vocoderInput = std::make_unique<Vo::VocoderStartInput>();
        vocoderInput->mel = mel;
        vocoderInput->f0 = f0;

        // Start inference
        std::unique_ptr<srt::TaskResult> resultOwner;
        Vo::VocoderResult *result = nullptr;
        if (auto exp = inference->start(*vocoderInput); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to start vocoder inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            resultOwner = exp.take();
            result = resultOwner->as<Vo::VocoderResult>();
        }
        audioData = std::move(result->audioData);
    }

    // Process audio data
    {
        using ds::WavFile;

        WavFile::DataFormat format{};
        format.container = WavFile::Container::RIFF;
        format.format = WavFile::WaveFormat::IEEE_FLOAT;
        format.channels = 1;
        format.sampleRate = 44100;
        format.bitsPerSample = 32;

        WavFile wav;
        if (!wav.init_file_write(outputWavPath, format)) {
            cliLog.srtCritical("Failed to initialize WAV writer.");
            return -1;
        }

        auto totalPCMFrameCount = audioData.size() / (format.channels * sizeof(float));

        auto framesWritten = wav.write_pcm_frames(totalPCMFrameCount, audioData.data());
        if (framesWritten != totalPCMFrameCount) {
            cliLog.srtCritical("Failed to write all frames.");
        }
        wav.close();

        cliLog.srtSuccess("Saved audio to " + stdc::path::to_utf8(outputWavPath));
    }

    return 0;
}

static inline std::string exception_message(const std::exception &e) {
    std::string msg = e.what();
#ifdef _WIN32
    if (typeid(e) == typeid(fs::filesystem_error)) {
        auto &err = static_cast<const fs::filesystem_error &>(e);
        msg = stdc::wstring_conv::to_utf8(stdc::wstring_conv::from_ansi(err.what()));
    }
#endif
    return msg;
}

int main(int /*argc*/, char * /*argv*/[]) {
    auto cmdline = stdc::system::command_line_arguments();
    if (cmdline.size() < 4) {
        stdc::u8println("Usage: %1 <package> <input> <output_wav> <ep> <device_index>",
                        stdc::system::application_name());
        return 1;
    }

    srt::Logger::setLogCallback(log_report_callback);

    const auto &packagePath = stdc::path::from_utf8(cmdline[1]);
    const auto &inputPath = stdc::path::from_utf8(cmdline[2]);
    const auto &outputWavPath = stdc::path::from_utf8(cmdline[3]);
    auto ep = EP::CPU;
    if (cmdline.size() >= 5) {
        const auto epString = stdc::to_lower(cmdline[4]);
        if (epString == "dml" || epString == "directml") {
            ep = EP::DML;
        } else if (epString == "cuda") {
            ep = EP::CUDA;
        } else if (epString == "coreml") {
            ep = EP::CoreML;
        }
    }
    int deviceIndex = 0;
    if (cmdline.size() >= 6) {
        try {
            deviceIndex = std::stoi(cmdline[5]);
        } catch (const std::invalid_argument &e) {
            deviceIndex = 0;
        } catch (const std::out_of_range &e) {
            deviceIndex = 0;
        }
    }

    int ret;
    try {
        ret = exec(packagePath, inputPath, outputWavPath, ep, deviceIndex);
    } catch (const std::exception &e) {
        std::string msg = exception_message(e);
        stdc::console::critical("Error: %1", msg);
        ret = -1;
    }
    return ret;
}
