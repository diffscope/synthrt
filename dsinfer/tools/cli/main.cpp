#include <filesystem>
#include <fstream>

#include <stdcorelib/system.h>
#include <stdcorelib/console.h>
#include <stdcorelib/path.h>

#include <synthrt/Core/SynthUnit.h>
#include <synthrt/Core/PackageRef.h>
#include <synthrt/Support/JSON.h>
#include <synthrt/Support/Logging.h>
#include <synthrt/SVS/SingerContrib.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/Inference.h>

#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceDriverPlugin.h>
#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

#include "AcousticInputParser.h"
#include "WavFile.h"

namespace fs = std::filesystem;

namespace Ac = ds::Api::Acoustic::L1;
namespace Vo = ds::Api::Vocoder::L1;

using srt::NO;

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

static void initializeSU(srt::SynthUnit &su) {
    // Get basic directories
    auto appDir = stdc::system::application_directory();
    auto defaultPluginDir =
        appDir.parent_path() / _TSTR("lib") / _TSTR("plugins") / _TSTR("dsinfer");

    // Set default plugin directories
    su.addPluginPath("org.openvpi.SingerProvider", defaultPluginDir / _TSTR("singerproviders"));
    su.addPluginPath("org.openvpi.InferenceDriver", defaultPluginDir / _TSTR("inferencedrivers"));
    su.addPluginPath("org.openvpi.InferenceInterpreter",
                     defaultPluginDir / _TSTR("inferenceinterpreters"));

    // Load driver
    auto plugin = su.plugin<ds::InferenceDriverPlugin>("onnx");
    if (!plugin) {
        throw std::runtime_error("failed to load inference driver");
    }

    auto onnxDriver = plugin->create();
    auto onnxArgs = NO<ds::Api::Onnx::DriverInitArgs>::create();

    // TODO: users should be able to configure these args
    onnxArgs->ep = ds::Api::Onnx::CPUExecutionProvider;
    onnxArgs->runtimePath = plugin->path().parent_path() / _TSTR("runtimes");
    onnxArgs->deviceIndex = 0;

    if (auto exp = onnxDriver->initialize(onnxArgs); !exp) {
        throw std::runtime_error(
            stdc::formatN(R"(failed to initialize onnx driver: %1)", exp.error().message()));
    }

    // Add driver
    auto &inferenceCate = *su.category("inference");
    inferenceCate.addObject("dsdriver", onnxDriver);
}

struct InputObject {
    std::string singer;
    NO<Ac::AcousticStartInput> input;

    static InputObject load(const fs::path &path, std::string *err) {
        // read all from path to string
        std::ifstream ifs(path);
        if (!ifs) {
            *err = stdc::formatN(R"(failed to open input file "%1")", path);
        }
        std::string jsonStr((std::istreambuf_iterator<char>(ifs)),
                            (std::istreambuf_iterator<char>()));

        // parse JSON
        srt::JsonValue jsonDoc;
        if (std::string err1;
            jsonDoc = srt::JsonValue::fromJson(jsonStr, true, err), !err->empty()) {
            return {};
        }
        if (!jsonDoc.isObject()) {
            *err = stdc::formatN("not an object");
            return {};
        }
        const auto &docObj = jsonDoc.toObject();
        InputObject res;
        {
            auto it = docObj.find("singer");
            if (it == docObj.end()) {
                *err = stdc::formatN("missing singer field");
                return {};
            }
            res.singer = it->second.toString();
            if (res.singer.empty()) {
                *err = stdc::formatN("empty singer field");
                return {};
            }

            // parse acoustic input
            if (auto exp = ds::parseAcousticStartInput(docObj); exp) {
                res.input = exp.take();
            } else {
                *err = exp.takeError().message();
                return {};
            }
        }
        return res;
    }
};

static int exec(const fs::path &packagePath, const fs::path &inputPath,
                const fs::path &outputWavPath) {
    // Read input
    InputObject input;
    if (std::string err; input = InputObject::load(inputPath, &err), !err.empty()) {
        throw std::runtime_error(
            stdc::formatN(R"(failed to read input file "%1": %2)", inputPath, err));
    }

    srt::SynthUnit su;
    initializeSU(su);

    // Load package
    srt::ScopedPackageRef pkg;
    if (auto exp = su.open(packagePath, false); !exp) {
        throw std::runtime_error(stdc::formatN(R"(failed to open package "%1": %2)", packagePath,
                                               exp.error().message()));
    } else {
        pkg = exp.take();
    }
    if (!pkg.isLoaded()) {
        throw std::runtime_error(stdc::formatN(R"(failed to load package "%1": %2)", packagePath,
                                               pkg.error().message()));
    }

    // Find singer
    auto &singerCate = *su.category("singer")->as<srt::SingerCategory>();
    const auto &singers = singerCate.singers();
    const srt::SingerSpec *singerSpec = nullptr;
    for (const auto &singer : singers) {
        if (singer->id() == input.singer) {
            singerSpec = singer;
            break;
        }
    }
    if (!singerSpec) {
        throw std::runtime_error(
            stdc::formatN(R"(singer "%1" not found in package)", input.singer));
    }

    struct ImportData {
        NO<srt::InferenceImportOptions> options;
        srt::InferenceSpec *inference = nullptr;
    };
    ImportData importAcoustic, importVocoder;

    for (const auto &import : singerSpec->imports()) {
        if (import.inference()->className() == Ac::API_CLASS) {
            importAcoustic = {
                import.options(),
                import.inference(),
            };
            if (importVocoder.inference) {
                break;
            }
            continue;
        }
        if (import.inference()->className() == Vo::API_CLASS) {
            importVocoder = {
                import.options(),
                import.inference(),
            };
            if (importAcoustic.inference) {
                break;
            }
        }
    }
    if (!importAcoustic.inference) {
        throw std::runtime_error(
            stdc::formatN(R"(acoustic inference not found for singer "%1")", input.singer));
    }
    if (!importVocoder.inference) {
        throw std::runtime_error(
            stdc::formatN(R"(vocoder inference not found for singer "%1")", input.singer));
    }

    // Run acoustic
    NO<ds::ITensor> mel;
    NO<ds::ITensor> f0;
    {
        // Prepare
        NO<srt::Inference> inference;
        if (auto exp = importAcoustic.inference->createInference(
                importAcoustic.options, NO<Ac::AcousticRuntimeOptions>::create());
            !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to create acoustic inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            inference = exp.take();
        }
        if (auto exp = inference->initialize(NO<Ac::AcousticInitArgs>::create()); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize acoustic inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        }

        // Start inference
        if (auto exp = inference->start(input.input); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to start acoustic inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        }
        auto result = inference->result().as<Ac::AcousticResult>();
        if (inference->state() == srt::ITask::Failed) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to run acoustic inference for singer "%1": %2)",
                              input.singer, result->error.message()));
        }
        mel = result->mel;
        f0 = result->f0;
    }

    // Run vocoder
    std::vector<uint8_t> audioData;
    {
        // Prepare
        NO<srt::Inference> inference;
        if (auto exp = importVocoder.inference->createInference(
                importVocoder.options, NO<Vo::VocoderRuntimeOptions>::create());
            !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to create vocoder inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        } else {
            inference = exp.take();
        }
        if (auto exp = inference->initialize(NO<Vo::VocoderInitArgs>::create()); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize vocoder inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        }

        auto vocoderInput = NO<Vo::VocoderStartInput>::create();
        vocoderInput->mel = mel;
        vocoderInput->f0 = f0;

        // Start inference
        if (auto exp = inference->start(vocoderInput); !exp) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to start vocoder inference for singer "%1": %2)",
                              input.singer, exp.error().message()));
        }
        auto result = inference->result().as<Vo::VocoderResult>();
        if (inference->state() == srt::ITask::Failed) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to run vocoder inference for singer "%1": %2)",
                              input.singer, result->error.message()));
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

        cliLog.srtSuccess("WAV file written successfully.");
    }

    cliLog.srtDebug("Debug: %1", stdc::system::application_name());
    cliLog.srtSuccess("Success: %1", stdc::system::application_name());
    cliLog.srtInfo("Info: %1", stdc::system::application_name());
    cliLog.srtWarning("Warning: %1", stdc::system::application_name());
    cliLog.srtCritical("Critical: %1", stdc::system::application_name());

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
        stdc::u8println("Usage: %1 <package> <input> <output_wav>",
                        stdc::system::application_name());
        return 1;
    }

    srt::Logger::setLogCallback(log_report_callback);

    const auto &packagePath = stdc::path::from_utf8(cmdline[1]);
    const auto &inputPath = stdc::path::from_utf8(cmdline[2]);
    const auto &outputWavPath = stdc::path::from_utf8(cmdline[3]);

    int ret;
    try {
        ret = exec(packagePath, inputPath, outputWavPath);
    } catch (const std::exception &e) {
        std::string msg = exception_message(e);
        stdc::console::critical("Error: %1", msg);
        ret = -1;
    }
    return ret;
}