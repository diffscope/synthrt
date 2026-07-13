#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <synthrt/Driver/onnx/OnnxDriverApi.h>

namespace dsinfer_cli {

    using EP = srt::driver::onnx::ExecutionProvider;

    struct CliArgs {
        std::string mode;                    // "midi" or "dspx"
        std::filesystem::path packageDir;    // voicebank directory (contains desc.json)
        std::filesystem::path inputPath;     // .mid or .dspx file
        std::string speakerId;
        std::filesystem::path outputDir;
        std::string languageId = "cmn";      // default language
        EP ep = EP::CPUExecutionProvider;
        int deviceIndex = 0;
        size_t maxSegments = 0;

        // Optional named parameters
        std::vector<std::filesystem::path> g2pPackagePaths;  // --g2p-packages
        std::vector<std::filesystem::path> pluginPaths;      // --plugin-paths (G2P categories)
        std::filesystem::path pluginRoot;                    // --plugin-root
        std::filesystem::path dumpDataDir;                   // --dump-data (empty = disabled)
        bool testLiteStyle = false;                          // --test-lite-style

        int exitCode = 0;  // Set to non-zero if parse fails (e.g. --help printed)

        /// Parse command-line arguments. Returns true on success, false if
        /// usage was printed or an error occurred (check exitCode).
        bool parse(int argc, char *argv[]);
    };

    void printUsage();

} // namespace dsinfer_cli
