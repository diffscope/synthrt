#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <synthrt/S2P/LanguageResource.h>

namespace {
    std::string escapeJson(const std::string &text) {
        std::string out;
        for (const char ch : text) {
            switch (ch) {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out += ch; break;
            }
        }
        return out;
    }

    void printResult(const std::vector<std::string> &phonemes, const std::vector<bool> &onsets) {
        std::printf("{\n  \"phonemes\": [");
        for (size_t i = 0; i < phonemes.size(); ++i) {
            std::printf("%s\"%s\"", i == 0 ? "" : ", ", escapeJson(phonemes[i]).c_str());
        }
        std::printf("],\n  \"onsets\": [");
        for (size_t i = 0; i < onsets.size(); ++i) {
            std::printf("%s%s", i == 0 ? "" : ", ", onsets[i] ? "true" : "false");
        }
        std::printf("]\n}\n");
    }

    void printUsage(const char *prog) {
        std::printf("%s\n\n", TOOL_DESC);
        std::printf("Usage:\n");
        std::printf("  %s direct <pronunciation> [onset.json]\n", prog);
        std::printf("  %s dict <dictionary.tsv> <pronunciation> [onset.json]\n", prog);
        std::printf("\nOptions:\n");
        std::printf("  -h, --help  Show this help message\n");
        std::printf("  --version   Show version\n");
    }
}

int main(int argc, char **argv) {
    if (argc >= 2 && std::string_view(argv[1]) == "--version") {
        std::printf("%s\n", TOOL_VERSION);
        return 0;
    }

    if (argc < 2 || std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
        printUsage(argv[0]);
        return argc < 2 ? 1 : 0;
    }

    try {
        srt::s2p::SyllablePronunciation result;
        const std::string cmd = argv[1];
        if (cmd == "direct") {
            if (argc < 3) {
                printUsage(argv[0]);
                return 1;
            }
            auto resource = srt::s2p::LanguageResource::direct(argc >= 4 ? argv[3] : "");
            result = resource.convert(argv[2]);
        } else if (cmd == "dict") {
            if (argc < 4) {
                printUsage(argv[0]);
                return 1;
            }
            auto resource = srt::s2p::LanguageResource::dictionary(argv[2], argc >= 5 ? argv[4] : "");
            result = resource.convert(argv[3]);
        } else {
            printUsage(argv[0]);
            return 1;
        }

        printResult(result.phonemes, result.onsets);
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
