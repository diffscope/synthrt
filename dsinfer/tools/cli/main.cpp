#include <filesystem>

#include <stdcorelib/system.h>
#include <stdcorelib/console.h>
#include <stdcorelib/path.h>

#include <synthrt/Core/SynthUnit.h>
#include <synthrt/Core/PackageRef.h>
#include <synthrt/SVS/SingerContrib.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/Inference.h>

#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceDriverPlugin.h>
#include <dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
#include <dsinfer/Api/Inferences/Vocoder/1/VocoderApiL1.h>

namespace fs = std::filesystem;

namespace Ac = ds::Api::Acoustic::L1;
namespace Vo = ds::Api::Vocoder::L1;

using srt::NO;

static void initializeSU(srt::SynthUnit &su) {
    // Get basic directories
    auto appDir = stdc::system::application_directory();
    auto defaultPluginDir =
        appDir.parent_path() / _TSTR("lib") / _TSTR("plugins") / _TSTR("dsinfer");

    // Set default plugin directories
    su.addPluginPath("ai.svs.InferenceDriver", defaultPluginDir / _TSTR("inferencedrivers"));
    su.addPluginPath("ai.svs.InferenceInterpreter",
                     defaultPluginDir / _TSTR("inferenceinterpreters"));

    // Load driver
    auto plugin = su.plugin<ds::InferenceDriverPlugin>("onnx");
    if (!plugin) {
        throw std::runtime_error("failed to load inference driver");
    }

    // Add driver
    auto &inferenceCate = *su.category("inference");
    inferenceCate.addObject("dsdriver", plugin->create());
}

struct InputObject {
    std::string singer;
    NO<Ac::AcousticStartInput> input;

    static InputObject load(const fs::path &path, std::string *err) {
        // TODO
        return {};
    }
};

static int exec(const fs::path &packagePath, const fs::path &inputPath) {
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
    if (srt::Error err; pkg = su.open(packagePath, false, &err), !err.ok()) {
        throw std::runtime_error(
            stdc::formatN(R"(failed to open package "%1": %2)", packagePath, err.message()));
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
        if (import.inference()->className() == "ai.svs.AcousticInference") {
            importAcoustic = {
                import.options(),
                import.inference(),
            };
            if (importVocoder.inference) {
                break;
            }
            continue;
        }
        if (import.inference()->className() == "ai.svs.VocoderInference") {
            importVocoder = {
                import.options(),
                import.inference(),
            };
            if (importVocoder.inference) {
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
    NO<ds::AbstractTensor> mel;
    {
        NO<srt::Inference> inference;
        if (srt::Error err;
            inference = importAcoustic.inference->createInference(
                importAcoustic.options, NO<Ac::AcousticRuntimeOptions>::create(), &err),
            !err.ok()) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to create acoustic inference for singer "%1": %2)",
                              input.singer, err.message()));
        }
        if (srt::Error err; !inference->initialize(NO<Ac::AcousticInitArgs>::create(), &err)) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize acoustic inference for singer "%1": %2)",
                              input.singer, err.message()));
        }

        // Start inference
        if (srt::Error err; !inference->start(input.input, &err)) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to start acoustic inference for singer "%1": %2)",
                              input.singer, err.message()));
        }
        auto result = inference->result().as<Ac::AcousticResult>();
        if (inference->state() == srt::AbstractTask::Failed) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to run acoustic inference for singer "%1": %2)",
                              input.singer, result->error.message()));
        }
        mel = result->mel;
    }

    // Run vocoder
    std::vector<uint8_t> audioData;
    {
        // Create vocoder
        NO<srt::Inference> inference;
        if (srt::Error err;
            inference = importVocoder.inference->createInference(
                importVocoder.options, NO<Vo::VocoderRuntimeOptions>::create(), &err),
            !err.ok()) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to create vocoder inference for singer "%1": %2)",
                              input.singer, err.message()));
        }
        if (srt::Error err; !inference->initialize(NO<Vo::VocoderInitArgs>::create(), &err)) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to initialize vocoder inference for singer "%1": %2)",
                              input.singer, err.message()));
        }

        auto vocoderInput = NO<Vo::VocoderStartInput>::create();
        vocoderInput->mel = mel;

        // Start inference
        if (srt::Error err; !inference->start(vocoderInput, &err)) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to start vocoder inference for singer "%1": %2)",
                              input.singer, err.message()));
        }
        auto result = inference->result().as<Vo::VocoderResult>();
        if (inference->state() == srt::AbstractTask::Failed) {
            throw std::runtime_error(
                stdc::formatN(R"(failed to run vocoder inference for singer "%1": %2)",
                              input.singer, result->error.message()));
        }
        audioData = std::move(result->audioData);
    }

    // Process audio data
    // TODO
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
    if (cmdline.size() < 3) {
        stdc::u8println("Usage: %1 <package> <input>", stdc::system::application_name());
        return 1;
    }

    const auto &packagePath = stdc::path::from_utf8(cmdline[1]);
    const auto &inputPath = stdc::path::from_utf8(cmdline[2]);

    int ret;
    try {
        ret = exec(packagePath, inputPath);
    } catch (const std::exception &e) {
        std::string msg = exception_message(e);
        stdc::console::critical("Error: %1", msg);
        ret = -1;
    }
    return ret;
}