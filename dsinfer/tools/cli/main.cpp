#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>

#include <stdcorelib/console.h>
#include <stdcorelib/path.h>
#include <stdcorelib/str.h>
#include <stdcorelib/support/commandline.h>
#include <stdcorelib/system.h>

#include <synthrt/Support/Logging.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

#include "CliRuntime.h"
#include "SynthesisInput.h"
#include "SynthesisRunner.h"

namespace fs = std::filesystem;

namespace {

    using ExecutionProvider = ds::Api::Onnx::ExecutionProvider;

    void logReport(int level, const srt::LogContext &context, const std::string_view &message) {
        using namespace srt;
        using namespace stdc;

        // Keep the runner output focused on progress, warnings, and failures.
        if (level < Logger::Success) {
            return;
        }

        const auto time = std::time(nullptr);
        const auto localTime = std::localtime(&time);

        std::stringstream stream;
        stream << std::put_time(localTime, "%Y-%m-%d %H:%M:%S");
        const auto timestamp = stream.str();

        int foreground;
        int background;
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

        const char *signature;
        switch (level) {
            case Logger::Trace:
                signature = "T";
                break;
            case Logger::Debug:
                signature = "D";
                break;
            case Logger::Success:
                signature = "S";
                break;
            case Logger::Warning:
                signature = "W";
                break;
            case Logger::Critical:
                signature = "C";
                break;
            case Logger::Fatal:
                signature = "F";
                break;
            default:
                signature = "I";
                break;
        }

        console::printf(console::nostyle, foreground, console::nocolor, "[%s] %-15s",
                        timestamp.c_str(), context.category);
        console::printf(console::nostyle, console::nocolor, background, " %s ", signature);
        console::printf(console::nostyle, console::nocolor, console::nocolor, "  ");
        console::println(console::nostyle, foreground, console::nocolor, message);
    }

    fs::path defaultPluginRoot() {
        // The build and install layouts both place bin and lib beside each other.
        return stdc::system::application_directory().parent_path() / STDC_TSTR("lib") /
               STDC_TSTR("plugins") / STDC_TSTR("dsinfer");
    }

    std::string exceptionMessage(const std::exception &exception) {
        std::string message = exception.what();
#ifdef _WIN32
        // MSVC reports filesystem paths in the active ANSI code page rather than UTF 8.
        if (typeid(exception) == typeid(fs::filesystem_error)) {
            const auto &error = static_cast<const fs::filesystem_error &>(exception);
            message = stdc::wstring_conv::to_utf8(stdc::wstring_conv::from_ansi(error.what()));
        }
#endif
        return message;
    }

    ExecutionProvider parseExecutionProvider(std::string value) {
        value = stdc::to_lower(std::move(value));
        if (value == "dml" || value == "directml") {
            return ExecutionProvider::DML;
        }
        if (value == "cuda") {
            return ExecutionProvider::CUDA;
        }
        if (value == "coreml") {
            return ExecutionProvider::CoreML;
        }
        return ExecutionProvider::CPU;
    }

    int runCommand(const stdc::cli::ParseResult &result) {
        const auto packagePath = stdc::path::from_utf8(*result.value<std::string>(0));
        const auto inputPath = stdc::path::from_utf8(*result.value<std::string>(1));
        const auto outputPath = stdc::path::from_utf8(*result.value<std::string>(2));

        auto pluginRoot = defaultPluginRoot();
        if (const auto value = result.valueForOption<std::string>("--plugin-root")) {
            pluginRoot = stdc::path::from_utf8(*value);
        }

        const auto executionProvider =
            parseExecutionProvider(result.valueForOption<std::string>("--ep").value_or("cpu"));
        const auto deviceIndex = result.valueForOption<int>("--device").value_or(0);
        if (deviceIndex < 0) {
            stdc::console::critical("Error: device index cannot be negative");
            return 1;
        }

        try {
            auto input = ds::cli::SynthesisInput::load(inputPath);
            if (!input) {
                throw std::runtime_error("failed to load synthesis input: " +
                                         input.error().message());
            }

            ds::cli::CliRuntime runtime(pluginRoot, executionProvider, deviceIndex);
            ds::cli::SynthesisRunner runner(runtime.synthUnit());
            runner.run(packagePath, input.take(), outputPath);
            return 0;
        } catch (const std::exception &exception) {
            stdc::console::critical("Error: %1", exceptionMessage(exception));
            return 1;
        }
    }

    stdc::cli::Parser createCommandLineParser() {
        // stdc::cli validates required paths, option values, and integer syntax before the handler.
        stdc::cli::Command command(
            stdc::system::application_name(),
            "Run a DiffSinger synthesis package from an acoustic input file.");
        command.addArgument(stdc::cli::Argument("package", "Installed Package directory."))
            .addArgument(stdc::cli::Argument("input", "Acoustic input JSON file."))
            .addArgument(stdc::cli::Argument("output", "Destination WAV file."))
            .addOption(stdc::cli::Option({"-e", "--ep"},
                                         "ONNX Runtime execution provider. Defaults to cpu.")
                           .arg(stdc::cli::Argument("provider")
                                    .expect({"cpu", "dml", "directml", "cuda", "coreml"})))
            .addOption(stdc::cli::Option({"-d", "--device"},
                                         "Execution provider device index. Defaults to 0.")
                           .arg(stdc::cli::Argument("index").type<int>()))
            .addOption(stdc::cli::Option("--plugin-root",
                                         "Directory containing dsinfer plugin categories.")
                           .arg("directory"))
            .addVersionOption(TOOL_VERSION)
            .addHelpOption(true)
            .setHandler(runCommand);

        stdc::cli::Parser parser(std::move(command));
        parser.setDisplayOptions(stdc::cli::Parser::ShowArgumentExpectedValues);
        return parser;
    }

}

int main(int, char *[]) {
    srt::Logger::setLogCallback(logReport);
    auto parser = createCommandLineParser();
    return parser.invoke(stdc::system::command_line_arguments(), 1);
}
