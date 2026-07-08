#include "CliArgs.h"

#include <filesystem>
#include <string>
#include <vector>

#include <stdcorelib/console.h>
#include <stdcorelib/path.h>
#include <stdcorelib/str.h>
#include <stdcorelib/system.h>

namespace fs = std::filesystem;

namespace dsinfer_cli {

namespace {

    // Extracted from main.cpp lines 1528-1540.
    EP parseEp(const std::string &value) {
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
        return EP::CPUExecutionProvider;
    }

    // Extracted from main.cpp lines 1542-1550.
    int parseDeviceIndex(const std::string &value) {
        try {
            return std::stoi(value);
        } catch (const std::invalid_argument &) {
            return 0;
        } catch (const std::out_of_range &) {
            return 0;
        }
    }

    // Extracted from main.cpp lines 1552-1560.
    size_t parseMaxSegments(const std::string &value) {
        try {
            return static_cast<size_t>(std::stoull(value));
        } catch (const std::invalid_argument &) {
            return 0;
        } catch (const std::out_of_range &) {
            return 0;
        }
    }

    // Split a semicolon-separated path list, mirroring the --g2p-packages /
    // --plugin-paths value format documented in dsinfer-cli-flow.md section 2.2.
    std::vector<fs::path> splitPaths(const std::string &value) {
        std::vector<fs::path> result;
        size_t start = 0;
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

    // Extracted from main.cpp's g2pPackagesDir() (lines 1482-1488): official G2P
    // package directory compiled in via DSINFER_CLI_G2P_PACKAGES_DIR.
    std::vector<fs::path> defaultG2pPackagePaths() {
#ifdef DSINFER_CLI_G2P_PACKAGES_DIR
        return {fs::path(DSINFER_CLI_G2P_PACKAGES_DIR)};
#else
        return {};
#endif
    }

    // Extracted from main.cpp's initializeSU() (lines 567-588): DLL plugin
    // directories derived from app_dir. Each entry corresponds to a plugin
    // subdirectory registered with addPluginPath() in the original flow.
    std::vector<fs::path> defaultPluginPaths() {
        auto appDir = stdc::system::application_directory();
        auto defaultPluginRoot = appDir.parent_path() / _TSTR("lib") / _TSTR("plugins");
        auto defaultDriverPluginDir = defaultPluginRoot / _TSTR("srt-driver");
        auto defaultDsinferPluginDir = defaultPluginRoot / _TSTR("dsinfer");
        auto g2pPluginDir = defaultPluginRoot / _TSTR("srt-g2p");
        return {
            defaultDsinferPluginDir / _TSTR("singerproviders"),
            defaultDriverPluginDir / _TSTR("inferencedrivers"),
            defaultDsinferPluginDir / _TSTR("inferenceinterpreters"),
            g2pPluginDir / _TSTR("G2ps"),
            g2pPluginDir / _TSTR("dict"),
        };
    }

} // namespace

// Extracted from main.cpp lines 1562-1568.
void printUsage() {
    stdc::u8println("Usage:");
    stdc::u8println("  %1 midi <package_dir> <filled.mid> <spk> <output_dir> [language] [ep] [device_index] [max_segments]",
                    stdc::system::application_name());
    stdc::u8println("  %1 dspx <package_dir> <project.dspx> <spk> <output_dir> [language] [ep] [device_index] [max_segments]",
                    stdc::system::application_name());
    stdc::u8println("Options:");
    stdc::u8println("  --test-lite-style  Use ModelSet for per-stage lazy load + lifecycle test");
    stdc::u8println("  --g2p-packages <paths>   Semicolon-separated G2P package directories");
    stdc::u8println("  --plugin-paths <paths>   Semicolon-separated plugin directories");
    stdc::u8println("  --dump-data <dir>        Dump intermediate data to directory");
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
    if (cmdline[1] == "-h" || cmdline[1] == "--help") {
        printUsage();
        exitCode = 0;
        return false;
    }

    // Separate named args (--g2p-packages / --plugin-paths) from positionals.
    // Named args consume the following token as their value, which may be a
    // semicolon-separated path list.
    std::vector<std::string> positionals;
    bool hasG2pPackages = false;
    bool hasPluginPaths = false;

    for (size_t i = 1; i < cmdline.size(); ++i) {
        const auto &arg = cmdline[i];
        if (arg == "--g2p-packages") {
            if (i + 1 >= cmdline.size()) {
                printUsage();
                exitCode = 1;
                return false;
            }
            g2pPackagePaths = splitPaths(cmdline[i + 1]);
            hasG2pPackages = true;
            ++i;
        } else if (arg == "--plugin-paths") {
            if (i + 1 >= cmdline.size()) {
                printUsage();
                exitCode = 1;
                return false;
            }
            pluginPaths = splitPaths(cmdline[i + 1]);
            hasPluginPaths = true;
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

    // Positional layout (after stripping program name):
    //   mode package_dir input spk output_dir [language] [ep] [device_index] [max_segments]
    if (positionals.size() < 5) {
        printUsage();
        exitCode = 1;
        return false;
    }

    mode = positionals[0];
    packageDir = stdc::path::from_utf8(positionals[1]);
    inputPath = stdc::path::from_utf8(positionals[2]);
    speakerId = positionals[3];
    outputDir = stdc::path::from_utf8(positionals[4]);

    if (positionals.size() >= 6) {
        languageId = positionals[5];
    }
    if (positionals.size() >= 7) {
        ep = parseEp(positionals[6]);
    }
    if (positionals.size() >= 8) {
        deviceIndex = parseDeviceIndex(positionals[7]);
    }
    if (positionals.size() >= 9) {
        maxSegments = parseMaxSegments(positionals[8]);
    }

    // Apply defaults for named args that were not provided on the command line.
    if (!hasG2pPackages) {
        g2pPackagePaths = defaultG2pPackagePaths();
    }
    if (!hasPluginPaths) {
        pluginPaths = defaultPluginPaths();
    }

    exitCode = 0;
    return true;
}

} // namespace dsinfer_cli
