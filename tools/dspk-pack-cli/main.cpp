#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <diffsinger/Bank/PackageParser.h>
#include <diffsinger/Bank/PackageValidator.h>

namespace fs = std::filesystem;
namespace ds_bank = ds::bank;

static std::string escapeJson(const std::string &text) {
    std::string out;
    for (const char ch : text) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += ch;
                break;
        }
    }
    return out;
}

static void printUsage(const char *prog) {
    std::printf("%s\n", TOOL_DESC);
    std::printf("\n");
    std::printf("Usage:\n");
    std::printf("  %s validate <dir>       Validate a DiffSinger package directory\n", prog);
    std::printf("  %s info <dir>           Print standard package metadata\n", prog);
    std::printf("  %s pack <dir> <output>  Pack a DiffSinger package directory into an archive\n",
                prog);
    std::printf("\n");
    std::printf("Options:\n");
    std::printf("  -h, --help    Show this help message\n");
}

static void printStringArray(const std::vector<std::string> &items) {
    std::printf("[");
    for (size_t i = 0; i < items.size(); ++i) {
        std::printf("%s\"%s\"", i == 0 ? "" : ", ", escapeJson(items[i]).c_str());
    }
    std::printf("]");
}

static void printPathMap(const std::map<std::string, fs::path> &items) {
    std::printf("{");
    size_t i = 0;
    for (const auto &[key, path] : items) {
        std::printf("%s\"%s\": \"%s\"", i++ == 0 ? "" : ", ", escapeJson(key).c_str(),
                    escapeJson(path.generic_string()).c_str());
    }
    std::printf("}");
}

static int doInfo(const std::string &dir) {
    fs::path packageDir(dir);
    if (!fs::exists(packageDir)) {
        std::fprintf(stderr, "error: directory does not exist: %s\n", dir.c_str());
        return 1;
    }

    ds_bank::PackageParser parser;
    auto result = parser.parsePackage(packageDir, ds_bank::PackageParser::ParseMode::Relaxed);
    if (!result) {
        std::fprintf(stderr, "error: failed to parse package: %s\n",
                     result.error().message().c_str());
        return 1;
    }

    const auto &info = result.value();
    std::printf("{\n");
    std::printf("  \"id\": \"%s\",\n", escapeJson(info.packageId()).c_str());
    std::printf("  \"version\": \"%s\",\n", escapeJson(info.version().toString()).c_str());
    std::printf("  \"name\": \"%s\",\n", escapeJson(info.name()).c_str());
    std::printf("  \"root\": \"%s\",\n", escapeJson(info.rootPath().generic_string()).c_str());
    std::printf("  \"singerRefs\": [");
    for (size_t i = 0; i < info.singerRefs().size(); ++i) {
        std::printf("%s\"%s\"", i == 0 ? "" : ", ", escapeJson(info.singerRefs()[i].generic_string()).c_str());
    }
    std::printf("],\n");
    std::printf("  \"inferenceRefs\": [");
    for (size_t i = 0; i < info.inferenceRefs().size(); ++i) {
        std::printf("%s\"%s\"", i == 0 ? "" : ", ", escapeJson(info.inferenceRefs()[i].generic_string()).c_str());
    }
    std::printf("],\n");
    std::printf("  \"inferences\": [");
    for (size_t i = 0; i < info.inferences().size(); ++i) {
        const auto &inference = info.inferences()[i];
        std::printf("%s{\"id\": \"%s\", \"class\": \"%s\", \"level\": %d, \"resourceCount\": %zu",
                    i == 0 ? "" : ", ", escapeJson(inference.id).c_str(),
                    escapeJson(inference.className).c_str(), inference.level,
                    inference.resourcePaths.size());
        std::printf(", \"phonemes\": \"%s\"", escapeJson(inference.phonemesPath.generic_string()).c_str());
        std::printf(", \"languages\": \"%s\"", escapeJson(inference.languagesPath.generic_string()).c_str());
        std::printf(", \"models\": ");
        printPathMap(inference.modelPaths);
        std::printf(", \"speakerEmbeddings\": ");
        printPathMap(inference.speakerEmbeddings);
        std::printf(", \"parameters\": ");
        printStringArray(inference.parameters);
        std::printf(", \"sampleRate\": %d, \"hopSize\": %d, \"hiddenSize\": %d, \"frameWidth\": %.15g",
                    inference.sampleRate, inference.hopSize, inference.hiddenSize,
                    inference.frameWidth);
        std::printf(", \"useLanguageId\": %s, \"useSpeakerEmbedding\": %s, \"useContinuousAcceleration\": %s}",
                    inference.useLanguageId ? "true" : "false",
                    inference.useSpeakerEmbedding ? "true" : "false",
                    inference.useContinuousAcceleration ? "true" : "false");
    }
    std::printf("],\n");
    std::printf("  \"singers\": [");
    for (size_t i = 0; i < info.singers().size(); ++i) {
        const auto &singer = info.singers()[i];
        std::printf("%s{\"id\": \"%s\", \"name\": \"%s\", \"defaultLanguage\": \"%s\"}",
                    i == 0 ? "" : ", ", escapeJson(singer.singerId()).c_str(),
                    escapeJson(singer.name()).c_str(), escapeJson(singer.defaultLanguage()).c_str());
    }
    std::printf("]\n");
    std::printf("}\n");
    return 0;
}

static int doValidate(const std::string &dir) {
    fs::path packageDir(dir);
    if (!fs::exists(packageDir)) {
        std::fprintf(stderr, "error: directory does not exist: %s\n", dir.c_str());
        return 1;
    }

    ds_bank::PackageValidator validator;
    auto report =
        validator.validatePackage(packageDir, ds_bank::PackageValidator::SchemaVersion::V10);

    if (report.items().empty()) {
        std::printf("OK: no issues found in %s\n", dir.c_str());
        return 0;
    }

    for (const auto &item : report.items()) {
        const char *sev = "INFO";
        if (item.severity == ds_bank::ValidationItem::Error) {
            sev = "ERROR";
        } else if (item.severity == ds_bank::ValidationItem::Warning) {
            sev = "WARN";
        }
        if (item.path.empty()) {
            std::printf("[%s] %s\n", sev, item.message.c_str());
        } else {
            std::printf("[%s] %s: %s\n", sev, item.path.c_str(), item.message.c_str());
        }
        if (!item.actualValue.empty()) {
            std::printf("      actual: %s\n", item.actualValue.c_str());
        }
        if (!item.recommendation.empty()) {
            std::printf("      recommendation: %s\n", item.recommendation.c_str());
        }
    }

    return report.hasErrors() ? 1 : 0;
}

static int doPack(const std::string &dir, const std::string &output) {
    fs::path packageDir(dir);
    if (!fs::exists(packageDir)) {
        std::fprintf(stderr, "error: directory does not exist: %s\n", dir.c_str());
        return 1;
    }

    // Validate first
    ds_bank::PackageValidator validator;
    auto report =
        validator.validatePackage(packageDir, ds_bank::PackageValidator::SchemaVersion::V10);
    if (report.hasErrors()) {
        std::fprintf(stderr, "error: package validation failed; refusing to pack\n");
        for (const auto &item : report.items()) {
            if (item.severity != ds_bank::ValidationItem::Error) {
                continue;
            }
            std::fprintf(stderr, "  [%s] %s", item.path.c_str(), item.message.c_str());
            if (!item.actualValue.empty()) {
                std::fprintf(stderr, " actual=%s", item.actualValue.c_str());
            }
            if (!item.recommendation.empty()) {
                std::fprintf(stderr, " recommendation=%s", item.recommendation.c_str());
            }
            std::fprintf(stderr, "\n");
        }
        return 1;
    }

    // Stub: archive creation is not yet implemented.
    std::fprintf(stderr, "error: packing is not yet implemented (output: %s)\n", output.c_str());
    (void) dir;
    return 2;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string_view cmd(argv[1]);
    if (cmd == "-h" || cmd == "--help") {
        printUsage(argv[0]);
        return 0;
    }

    if (cmd == "validate") {
        if (argc < 3) {
            std::fprintf(stderr, "error: validate requires a <dir> argument\n");
            return 1;
        }
        return doValidate(argv[2]);
    }

    if (cmd == "info") {
        if (argc < 3) {
            std::fprintf(stderr, "error: info requires a <dir> argument\n");
            return 1;
        }
        return doInfo(argv[2]);
    }

    if (cmd == "pack") {
        if (argc < 4) {
            std::fprintf(stderr, "error: pack requires <dir> and <output> arguments\n");
            return 1;
        }
        return doPack(argv[2], argv[3]);
    }

    std::fprintf(stderr, "error: unknown command '%s'\n", argv[1]);
    printUsage(argv[0]);
    return 1;
}
