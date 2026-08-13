#include "CliArgs.h"

#include <stdcorelib/console.h>
#include <stdcorelib/path.h>
#include <stdcorelib/str.h>
#include <stdcorelib/system.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "RuntimeLayout.h"

namespace fs = std::filesystem;

namespace dsinfer_cli {

    namespace {

        // Extracted from main.cpp lines 1528-1540.
        // Returns std::nullopt on unknown value (BUG-CLI-005: avoid silent fallback
        // to CPUExecutionProvider which hides user typos).
        std::optional<EP> parseEp(const std::string &value) {
            const auto epString = stdc::to_lower(value);
            if (epString == "dml" || epString == "directml") {
                return EP::DMLExecutionProvider;
            }
            if (epString == "cuda") {
                return EP::CUDAExecutionProvider;
            }
            if (epString == "coreml") {
                return EP::CoreMLExecutionProvider;
            }
            if (epString == "cpu") {
                return EP::CPUExecutionProvider;
            }
            stdc::u8println("error: unknown execution provider '%1'", value);
            return std::nullopt;
        }

        // Extracted from main.cpp lines 1542-1550.
        // Returns std::nullopt on parse failure (BUG-CLI-004: avoid silent
        // fallback to device 0 which hides invalid numeric input).
        std::optional<int> parseDeviceIndex(const std::string &value) {
            try {
                return std::stoi(value);
            } catch (const std::invalid_argument &) {
                stdc::u8println("error: invalid device index '%1'", value);
                return std::nullopt;
            } catch (const std::out_of_range &) {
                stdc::u8println("error: device index out of range '%1'", value);
                return std::nullopt;
            }
        }

        // Split a semicolon-separated path list, mirroring the --g2p-packages /
        // --plugin-paths value format documented in dsinfer-cli-flow.md section 2.2.
        std::vector<fs::path> splitPaths(const std::string &value) {
            std::vector<fs::path> result;
            size_t                start = 0;
            while (start <= value.size()) {
                const auto sep = value.find(';', start);
                if (sep == std::string::npos) {
                    auto part = value.substr(start);
                    if (!part.empty()) {
                        result.push_back(stdc::path::from_utf8(part));
                    }
                    break;
                }
                auto part = value.substr(start, sep - start);
                if (!part.empty()) {
                    result.push_back(stdc::path::from_utf8(part));
                }
                start = sep + 1;
            }
            return result;
        }

        std::vector<fs::path> defaultG2pPackagePaths() {
            return {synthrt::tools::runtime_layout::g2pPackagesRoot(stdc::system::application_directory())};
        }

        // Extracted from main.cpp's initializeSU() (lines 567-588): DLL plugin
        // directories derived from app_dir. Each entry corresponds to a plugin
        // subdirectory registered with addPluginPath() in the original flow.
        std::vector<fs::path> defaultPluginPaths(const fs::path &pluginRoot) {
            auto g2pPluginDir = pluginRoot / "srt-g2p";
            return {
                g2pPluginDir / "G2ps",
                g2pPluginDir / "dict",
            };
        }

    } // namespace

    // Extracted from main.cpp lines 1562-1568.
    void printUsage() {
        stdc::u8println("%1", TOOL_DESC);
        stdc::u8println("");
        stdc::u8println("Usage:");
        stdc::u8println("  %1 midi <package_dir> <filled.mid> <spk> <output_dir> [language] [ep] [device_index]",
                        stdc::system::application_name());
        stdc::u8println("  %1 dspx <package_dir> <project.dspx> <spk> <output_dir> [language] [ep] [device_index]",
                        stdc::system::application_name());
        stdc::u8println("Options:");
        stdc::u8println("  --test-lite-style  Use ModelSet for per-stage lazy load + lifecycle test");
        stdc::u8println("  --g2p-packages <paths>   Semicolon-separated G2P package directories");
        stdc::u8println("  --plugin-paths <paths>   Semicolon-separated G2P plugin category directories");
        stdc::u8println("  --plugin-root <dir>      Plugin root containing srt-driver, diffsinger, and srt-g2p");
        stdc::u8println("  --dump-data <dir>        Dump intermediate data to directory");
        stdc::u8println("  -h, --help               Show this help message");
        stdc::u8println("  --version                Show version");
    }

    bool CliArgs::parse(int argc, char *argv[]) {
        (void)argc;
        (void)argv;

        // main.cpp reads the raw command line via stdc::system::command_line_arguments()
        // rather than argc/argv; preserve that behavior for consistent UTF-8 handling.
        auto cmdline = stdc::system::command_line_arguments();

        if (cmdline.size() < 2) {
            printUsage();
            exitCode = 1;
            return false;
        }
        if (cmdline[1] == "--version") {
            stdc::u8println("%1", TOOL_VERSION);
            exitCode = 0;
            return false;
        }
        if (cmdline[1] == "-h" || cmdline[1] == "--help") {
            printUsage();
            exitCode = 0;
            return false;
        }

        // Separate named args from positionals.
        // Named args consume the following token as their value, which may be a
        // semicolon-separated path list.
        std::vector<std::string> positionals;
        bool                     hasG2pPackages = false;
        bool                     hasPluginPaths = false;

        for (size_t i = 1; i < cmdline.size(); ++i) {
            const auto &arg = cmdline[i];
            if (arg == "--g2p-packages") {
                if (i + 1 >= cmdline.size()) {
                    printUsage();
                    exitCode = 1;
                    return false;
                }
                g2pPackagePaths = splitPaths(cmdline[i + 1]);
                hasG2pPackages  = true;
                ++i;
            } else if (arg == "--plugin-paths") {
                if (i + 1 >= cmdline.size()) {
                    printUsage();
                    exitCode = 1;
                    return false;
                }
                pluginPaths    = splitPaths(cmdline[i + 1]);
                hasPluginPaths = true;
                ++i;
            } else if (arg == "--plugin-root") {
                if (i + 1 >= cmdline.size()) {
                    printUsage();
                    exitCode = 1;
                    return false;
                }
                pluginRoot = stdc::path::from_utf8(cmdline[i + 1]);
                ++i;
            } else if (arg == "--dump-data") {
                if (i + 1 >= cmdline.size()) {
                    printUsage();
                    exitCode = 1;
                    return false;
                }
                dumpDataDir = stdc::path::from_utf8(cmdline[i + 1]);
                ++i;
            } else if (arg == "--test-lite-style") {
                testLiteStyle = true;
            } else {
                positionals.push_back(arg);
            }
        }

        if (!positionals.empty() && positionals[0] != "midi" && positionals[0] != "dspx") {
            stdc::u8println("error: unknown mode '%1'", positionals[0]);
            printUsage();
            exitCode = 1;
            return false;
        }

        // Positional layout (after stripping program name):
        //   mode package_dir input spk output_dir [language] [ep] [device_index]
        if (positionals.size() < 5) {
            printUsage();
            exitCode = 1;
            return false;
        }

        mode       = positionals[0];
        packageDir = stdc::path::from_utf8(positionals[1]);
        inputPath  = stdc::path::from_utf8(positionals[2]);
        speakerId  = positionals[3];
        outputDir  = stdc::path::from_utf8(positionals[4]);

        if (positionals.size() >= 6) {
            languageId = positionals[5];
        }
        if (positionals.size() >= 7) {
            auto parsed = parseEp(positionals[6]);
            if (!parsed) {
                printUsage();
                exitCode = 1;
                return false;
            }
            ep = *parsed;
        }
        if (positionals.size() >= 8) {
            auto parsed = parseDeviceIndex(positionals[7]);
            if (!parsed) {
                printUsage();
                exitCode = 1;
                return false;
            }
            deviceIndex = *parsed;
        }

        // Apply defaults for named args that were not provided on the command line.
        if (!hasG2pPackages) {
            g2pPackagePaths = defaultG2pPackagePaths();
        }
        if (pluginRoot.empty()) {
            pluginRoot = synthrt::tools::runtime_layout::pluginRoot(stdc::system::application_directory());
        }
        if (!hasPluginPaths) {
            pluginPaths = defaultPluginPaths(pluginRoot);
        }

        exitCode = 0;
        return true;
    }

} // namespace dsinfer_cli
