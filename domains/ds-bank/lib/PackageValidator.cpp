#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <stdcorelib/str.h>

#include <synthrt/Core/Support/JSON.h>

#include <diffsinger/Bank/PackagePathResolver.h>
#include <diffsinger/Bank/PackageValidator.h>

using srt::core::JsonArray;
using srt::core::JsonObject;
using srt::core::JsonValue;

namespace ds::bank {

    namespace {
        constexpr auto Error = ValidationReport::Severity::Error;
        constexpr auto Warning = ValidationReport::Severity::Warning;
        constexpr auto Info = ValidationReport::Severity::Info;

        std::string readText(const std::filesystem::path &path, bool &ok) {
            std::ifstream file(path);
            if (!file.is_open()) {
                ok = false;
                return {};
            }
            std::stringstream ss;
            ss << file.rdbuf();
            ok = true;
            return ss.str();
        }

        std::string jsonPath(const std::filesystem::path &file, const std::string &ptr = {}) {
            auto result = file.generic_string();
            if (!ptr.empty()) {
                result += "#";
                result += ptr[0] == '/' ? ptr : "/" + ptr;
            }
            return result;
        }

        std::string typeName(const JsonValue &value) {
            switch (value.type()) {
                case JsonValue::Null:
                    return "null";
                case JsonValue::Bool:
                    return "boolean";
                case JsonValue::Double:
                case JsonValue::Int:
                case JsonValue::UInt:
                    return "number";
                case JsonValue::String:
                    return "string";
                case JsonValue::Array:
                    return "array";
                case JsonValue::Object:
                    return "object";
                default:
                    return "unknown";
            }
        }

        std::string actualValue(const JsonValue &value) {
            if (value.isString()) {
                return '"' + value.toString() + '"';
            }
            return value.toJson(-1);
        }

        std::string displayTextHint() {
            // ds-spec 2.4 §多语言文本：对象形态必须带 "_" 默认项；其余键对
            // Runtime 不透明（不做任何匹配），推荐 BCP 47 写法如 zh-CN。
            return R"(Use a string or localized object such as {"_":"Name","zh-CN":"名称"}.)";
        }

        void addMissing(ValidationReport &report, const std::filesystem::path &file,
                        const std::string &ptr, const std::string &recommended) {
            report.add(Error, "missing required key", jsonPath(file, ptr), "<missing>",
                       recommended);
        }

        void addTypeError(ValidationReport &report, const std::filesystem::path &file,
                          const std::string &ptr, const JsonValue &value,
                          const std::string &expected) {
            report.add(Error, "invalid value type", jsonPath(file, ptr),
                       stdc::formatN("%1: %2", typeName(value), actualValue(value)),
                       "Expected " + expected + ".");
        }

        bool hasKey(const JsonObject &obj, const std::string &key) {
            return obj.find(key) != obj.end();
        }

        const JsonValue *findKey(const JsonObject &obj, const std::string &key) {
            auto it = obj.find(key);
            return it == obj.end() ? nullptr : &it->second;
        }

        bool isDisplayText(const JsonValue &value) {
            if (value.isString()) {
                return true;
            }
            if (!value.isObject()) {
                return false;
            }
            for (const auto &[_, v] : value.toObject()) {
                if (!v.isString()) {
                    return false;
                }
            }
            return true;
        }

        std::string getString(const JsonObject &obj, const std::string &key) {
            if (const auto *value = findKey(obj, key); value && value->isString()) {
                return value->toString();
            }
            return {};
        }

        JsonValue parseJsonFile(ValidationReport &report, const std::filesystem::path &file) {
            bool ok = false;
            auto text = readText(file, ok);
            if (!ok) {
                report.add(Error, "failed to open JSON file", file.generic_string(), "<missing>",
                           "Create the file or fix the reference path.");
                return JsonValue(JsonValue::Undefined);
            }

            std::string parseError;
            auto root = JsonValue::fromJson(text, true, &parseError);
            if (!parseError.empty()) {
                report.add(Error, "invalid JSON", file.generic_string(), parseError,
                           "Fix JSON syntax before packaging.");
                return JsonValue(JsonValue::Undefined);
            }
            if (!root.isObject()) {
                addTypeError(report, file, "", root, "object");
                return JsonValue(JsonValue::Undefined);
            }
            return root;
        }

        void validateExtraKeys(ValidationReport &report, const std::filesystem::path &file,
                               const JsonObject &obj, const std::set<std::string> &allowed,
                               const std::string &ptr, const std::string &note) {
            for (const auto &[key, value] : obj) {
                if (!allowed.count(key)) {
                    auto keyPtr = ptr.empty() ? key : ptr + "/" + key;
                    report.add(Warning, "extra key not defined by the standard schema",
                               jsonPath(file, keyPtr), actualValue(value), note);
                }
            }
        }

        void requireString(ValidationReport &report, const std::filesystem::path &file,
                           const JsonObject &obj, const std::string &key,
                           const std::string &ptr, const std::string &example) {
            const auto *value = findKey(obj, key);
            const auto keyPtr = ptr.empty() ? key : ptr + "/" + key;
            if (!value) {
                addMissing(report, file, keyPtr, "Add key with string value, for example " + example + ".");
                return;
            }
            if (!value->isString()) {
                addTypeError(report, file, keyPtr, *value, "string");
            }
        }

        void optionalString(ValidationReport &report, const std::filesystem::path &file,
                            const JsonObject &obj, const std::string &key, const std::string &ptr) {
            const auto *value = findKey(obj, key);
            if (value && !value->isString()) {
                addTypeError(report, file, ptr.empty() ? key : ptr + "/" + key, *value, "string");
            }
        }

        void optionalDisplayText(ValidationReport &report, const std::filesystem::path &file,
                                 const JsonObject &obj, const std::string &key,
                                 const std::string &ptr) {
            const auto *value = findKey(obj, key);
            if (!value) {
                return;
            }
            const auto ptrHere = ptr.empty() ? key : ptr + "/" + key;
            if (!isDisplayText(*value)) {
                report.add(Error, "invalid display text", jsonPath(file, ptrHere),
                           actualValue(*value), displayTextHint());
                return;
            }
            // Shape checks above only verified that every value is a string.
            // ds-spec 2.4 §多语言文本 additionally requires the "_" default
            // entry. Non-"_" keys are opaque to the Runtime (it performs no
            // matching), so a POSIX-style separator such as zh_CN is legal
            // data — but front-ends conventionally resolve BCP 47 names, so
            // surface it as a Warning pointing at the recommended spelling.
            // The scanner tolerates all of this (legacy packages keep
            // loading).
            if (!value->isObject()) {
                return; // plain string short form: nothing more to check
            }
            const auto &textObj = value->toObject();
            if (textObj.find("_") == textObj.end()) {
                report.add(Error, "display text missing required \"_\" default entry",
                           jsonPath(file, ptrHere), actualValue(*value),
                           R"(Add a "_" entry with the fallback text (ds-spec 2.4).)");
            }
            for (const auto &[tag, v] : textObj) {
                if (tag == "_") {
                    continue;
                }
                if (tag.find('_') != std::string::npos) {
                    auto bcp47 = tag;
                    std::replace(bcp47.begin(), bcp47.end(), '_', '-');
                    report.add(Warning, "language tag uses a POSIX-style separator; BCP 47 recommended",
                               jsonPath(file, ptrHere + "/" + tag), actualValue(*value),
                               stdc::formatN(R"(Consider renaming "%1" to "%2": the Runtime treats keys as opaque and case-sensitive, and front-ends usually resolve BCP 47 spellings.)",
                                             tag, bcp47));
                }
            }
        }

        bool validVersionText(const std::string &value) {
            static const std::regex re(R"(^[0-9]+\.[0-9]+(\.[0-9]+)?(\.[0-9]+)?$)");
            return std::regex_match(value, re);
        }

        bool validIdText(const std::string &value) {
            return !value.empty() && value.find_first_of(R"(/\[]:;'" )") == std::string::npos;
        }

        void validatePathValue(ValidationReport &report, const std::filesystem::path &packageRoot,
                               const std::filesystem::path &baseDir, const std::filesystem::path &file,
                               const std::string &ptr, const JsonValue &value,
                               bool mustExist, bool allowDirectory) {
            if (!value.isString()) {
                addTypeError(report, file, ptr, value, "string path");
                return;
            }
            auto result = PackagePathResolver::resolve(packageRoot, baseDir, value.toString());
            if (!result.hasValue()) {
                report.add(Error, "invalid package resource path", jsonPath(file, ptr),
                           value.toString(), result.error().message());
                return;
            }
            const auto resolved = result.value();
            if (mustExist && !std::filesystem::exists(resolved)) {
                report.add(Error, "referenced path does not exist", jsonPath(file, ptr),
                           value.toString(), "Create this file or update the relative path.");
                return;
            }
            if (mustExist && !allowDirectory && std::filesystem::is_directory(resolved)) {
                report.add(Error, "expected a file but found a directory", jsonPath(file, ptr),
                           value.toString(), "Point this key to a file.");
            }
        }

        void validatePathArray(ValidationReport &report, const std::filesystem::path &packageRoot,
                               const std::filesystem::path &baseDir, const std::filesystem::path &file,
                               const std::string &ptr, const JsonValue &value,
                               bool mustExist, bool allowDirectory) {
            if (value.isString()) {
                validatePathValue(report, packageRoot, baseDir, file, ptr, value, mustExist, allowDirectory);
                return;
            }
            if (!value.isArray()) {
                addTypeError(report, file, ptr, value, "string path or array of string paths");
                return;
            }
            const auto &array = value.toArray();
            for (size_t i = 0; i < array.size(); ++i) {
                validatePathValue(report, packageRoot, baseDir, file,
                                  stdc::formatN("%1/%2", ptr, i), array[i], mustExist, allowDirectory);
            }
        }

        void validateConfigLanguage(ValidationReport &report, const std::filesystem::path &packageRoot,
                                    const std::filesystem::path &file, const JsonObject &lang,
                                    const std::string &ptr, std::set<std::string> &languageIds) {
            static const std::set<std::string> allowed = {
                "id", "name", "g2p", "dict", "onsetFile", "onsetMode", "s2pMode",
                "s2pFile", "g2pPackages", "g2pPackageVersion",
            };
            validateExtraKeys(report, file, lang, allowed, ptr,
                              "Remove the key or put application-specific data under a documented extension block.");
            requireString(report, file, lang, "id", ptr, R"("cmn")");
            optionalDisplayText(report, file, lang, "name", ptr);
            requireString(report, file, lang, "g2p", ptr, R"("g2p-cmn-official")");
            optionalString(report, file, lang, "onsetMode", ptr);
            optionalString(report, file, lang, "s2pMode", ptr);
            optionalString(report, file, lang, "g2pPackageVersion", ptr);

            const auto id = getString(lang, "id");
            if (!id.empty()) {
                languageIds.insert(id);
            }
            const auto g2p = getString(lang, "g2p");
            if (g2p.empty() || g2p == "unknown") {
                report.add(Error, "language is not routable", jsonPath(file, ptr + "/g2p"),
                           g2p.empty() ? "<missing>" : g2p,
                           R"(Use a concrete G2P id such as "g2p-cmn-official".)");
            }

            const auto baseDir = file.parent_path();
            if (const auto *dict = findKey(lang, "dict")) {
                validatePathValue(report, packageRoot, baseDir, file, ptr + "/dict", *dict, true, false);
            }
            if (const auto *s2pFile = findKey(lang, "s2pFile")) {
                validatePathValue(report, packageRoot, baseDir, file, ptr + "/s2pFile", *s2pFile, true, false);
            }
            if (const auto *onsetFile = findKey(lang, "onsetFile")) {
                validatePathValue(report, packageRoot, baseDir, file, ptr + "/onsetFile", *onsetFile, true, false);
            }
            if (const auto *packages = findKey(lang, "g2pPackages")) {
                validatePathArray(report, packageRoot, baseDir, file, ptr + "/g2pPackages", *packages, true, true);
            }

            const auto s2pMode = getString(lang, "s2pMode");
            if ((s2pMode == "map" || s2pMode == "custom") && !hasKey(lang, "s2pFile")) {
                addMissing(report, file, ptr + "/s2pFile",
                           "Add s2pFile for map/custom mode.");
            }
            if (s2pMode == "dict" && !hasKey(lang, "s2pFile") && !hasKey(lang, "dict")) {
                addMissing(report, file, ptr + "/dict",
                           "Add dict, or add s2pFile if the S2P dictionary is separate.");
            }

            const auto onsetMode = getString(lang, "onsetMode");
            if ((onsetMode == "rule" || onsetMode == "custom") && !hasKey(lang, "onsetFile")) {
                addMissing(report, file, ptr + "/onsetFile",
                           "Add onsetFile for rule/custom onset mode.");
            }
        }

        std::string validateInferenceConfig(ValidationReport &report,
                                            const std::filesystem::path &packageRoot,
                                            const std::filesystem::path &file) {
            auto root = parseJsonFile(report, file);
            if (!root.isObject()) {
                return {};
            }
            const auto &obj = root.toObject();
            static const std::set<std::string> allowed = {
                "$version", "id", "class", "level", "name", "schema", "configuration",
            };
            validateExtraKeys(report, file, obj, allowed, "",
                              "Remove the key or put backend-specific values under configuration.");
            optionalString(report, file, obj, "$version", "");
            optionalString(report, file, obj, "id", "");
            optionalString(report, file, obj, "class", "");
            optionalDisplayText(report, file, obj, "name", "");
            const auto *level = findKey(obj, "level");
            if (!level) {
                addMissing(report, file, "level", "Add numeric API level, for example 1.");
            } else if (!level->isInt()) {
                addTypeError(report, file, "level", *level, "integer");
            }
            if (const auto *schema = findKey(obj, "schema"); schema && !schema->isObject()) {
                addTypeError(report, file, "schema", *schema, "object");
            }
            if (const auto *config = findKey(obj, "configuration")) {
                if (!config->isObject()) {
                    addTypeError(report, file, "configuration", *config, "object");
                } else {
                    const auto &cfg = config->toObject();
                    const auto baseDir = file.parent_path();
                    for (const auto &[key, value] : cfg) {
                        const auto ptr = "configuration/" + key;
                        const bool pathLike = key == "model" || key == "encoder" || key == "predictor" ||
                                              key == "phonemes" || key == "languages" ||
                                              key.size() >= 5 && key.substr(key.size() - 5) == "Model";
                        if (pathLike && value.isString()) {
                            validatePathValue(report, packageRoot, baseDir, file, ptr, value, true, false);
                        }
                        if (key == "speakers") {
                            if (!value.isObject()) {
                                addTypeError(report, file, ptr, value, "object mapping speaker id to emb path");
                            } else {
                                for (const auto &[speaker, emb] : value.toObject()) {
                                    validatePathValue(report, packageRoot, baseDir, file, ptr + "/" + speaker,
                                                      emb, true, false);
                                }
                            }
                        }
                    }
                }
            }
            return getString(obj, "id");
        }

        void validateSingerConfig(ValidationReport &report, const std::filesystem::path &packageRoot,
                                  const std::filesystem::path &file,
                                  const std::set<std::string> &inferenceIds) {
            auto root = parseJsonFile(report, file);
            if (!root.isObject()) {
                return;
            }
            const auto &obj = root.toObject();
            static const std::set<std::string> allowed = {
                "$version", "id", "class", "level", "name", "avatar", "background",
                "demoAudio", "imports", "configuration",
            };
            validateExtraKeys(report, file, obj, allowed, "",
                              "Remove the key or put singer-provider-specific data under configuration.");
            requireString(report, file, obj, "$version", "", R"("1.0")");
            optionalString(report, file, obj, "id", "");
            optionalString(report, file, obj, "class", "");
            optionalDisplayText(report, file, obj, "name", "");
            const auto *level = findKey(obj, "level");
            if (!level) {
                addMissing(report, file, "level", "Add numeric Singer API level, for example 1.");
            } else if (!level->isInt()) {
                addTypeError(report, file, "level", *level, "integer");
            }
            const auto baseDir = file.parent_path();
            for (const auto &key : {std::string("avatar"), std::string("background"), std::string("demoAudio")}) {
                if (const auto *value = findKey(obj, key)) {
                    validatePathValue(report, packageRoot, baseDir, file, key, *value, true, false);
                }
            }
            if (const auto *imports = findKey(obj, "imports")) {
                if (!imports->isArray()) {
                    addTypeError(report, file, "imports", *imports, "array");
                } else {
                    const auto &array = imports->toArray();
                    for (size_t i = 0; i < array.size(); ++i) {
                        const auto ptr = stdc::formatN("imports/%1", i);
                        if (!array[i].isObject()) {
                            addTypeError(report, file, ptr, array[i], "object");
                            continue;
                        }
                        const auto &imp = array[i].toObject();
                        static const std::set<std::string> importAllowed = {"id", "options"};
                        validateExtraKeys(report, file, imp, importAllowed, ptr,
                                          R"(Use standard key "id". If this came from older "inferenceId", rename it.)");
                        requireString(report, file, imp, "id", ptr, R"("duration")");
                        const auto id = getString(imp, "id");
                        if (!id.empty() && id.find('/') == std::string::npos && !inferenceIds.count(id)) {
                            report.add(Error, "import references an unknown inference id",
                                       jsonPath(file, ptr + "/id"), id,
                                       "Add the inference config to desc.json contributes.inferences or fix the id.");
                        }
                        if (const auto *options = findKey(imp, "options"); options && !options->isObject()) {
                            addTypeError(report, file, ptr + "/options", *options, "object");
                        }
                    }
                }
            }
            if (const auto *config = findKey(obj, "configuration")) {
                if (!config->isObject()) {
                    addTypeError(report, file, "configuration", *config, "object");
                    return;
                }
                const auto &cfg = config->toObject();
                static const std::set<std::string> cfgAllowed = {
                    "defaultLanguage", "speakers", "languages", "version", "phonemeLength",
                };
                validateExtraKeys(report, file, cfg, cfgAllowed, "configuration",
                                  "Remove the key or document it as singer-provider configuration.");
                optionalString(report, file, cfg, "defaultLanguage", "configuration");
                std::set<std::string> languageIds;
                if (const auto *speakers = findKey(cfg, "speakers")) {
                    if (!speakers->isArray()) {
                        addTypeError(report, file, "configuration/speakers", *speakers, "array");
                    } else {
                        const auto &array = speakers->toArray();
                        for (size_t i = 0; i < array.size(); ++i) {
                            const auto ptr = stdc::formatN("configuration/speakers/%1", i);
                            if (!array[i].isObject()) {
                                addTypeError(report, file, ptr, array[i], "object");
                                continue;
                            }
                            const auto &speaker = array[i].toObject();
                            static const std::set<std::string> speakerAllowed = {"id", "name", "toneRanges"};
                            validateExtraKeys(report, file, speaker, speakerAllowed, ptr,
                                              "Remove the key or place extended speaker data under a documented extension block.");
                            requireString(report, file, speaker, "id", ptr, R"("main")");
                            optionalDisplayText(report, file, speaker, "name", ptr);
                        }
                    }
                }
                if (const auto *langs = findKey(cfg, "languages")) {
                    if (!langs->isArray()) {
                        addTypeError(report, file, "configuration/languages", *langs, "array");
                    } else {
                        const auto &array = langs->toArray();
                        for (size_t i = 0; i < array.size(); ++i) {
                            const auto ptr = stdc::formatN("configuration/languages/%1", i);
                            if (!array[i].isObject()) {
                                addTypeError(report, file, ptr, array[i], "object");
                                continue;
                            }
                            validateConfigLanguage(report, packageRoot, file, array[i].toObject(), ptr, languageIds);
                        }
                    }
                }
                const auto defaultLanguage = getString(cfg, "defaultLanguage");
                if (!defaultLanguage.empty() && !languageIds.empty() && !languageIds.count(defaultLanguage)) {
                    report.add(Error, "defaultLanguage is not declared in configuration.languages",
                               jsonPath(file, "configuration/defaultLanguage"), defaultLanguage,
                               "Add a matching language entry or change defaultLanguage.");
                }
            }
        }
    }

    ValidationReport PackageValidator::validate(const PackageManifest &info,
                                                SchemaVersion version) const {
        ValidationReport report;

        if (info.packageId().empty()) {
            report.add(Error, "missing required field: packageId", "packageId");
        }
        if (info.name().isEmpty()) {
            report.add(Error, "missing required field: name", "name");
        }
        if (info.version().isEmpty()) {
            report.add(Error, "missing required field: version", "version");
        }

        size_t idx = 0;
        for (const auto &singer : info.singers()) {
            if (singer.singerId().empty()) {
                report.add(Error, stdc::formatN("singers[%1]: missing singerId", idx),
                           stdc::formatN("singers/%1/singerId", idx));
            }
            ++idx;
        }

        (void) version;
        return report;
    }

    ValidationReport PackageValidator::validatePackage(const std::filesystem::path &packageDir,
                                                       SchemaVersion version) const {
        (void) version;
        ValidationReport report;
        std::error_code rootError;
        const auto packageRoot = std::filesystem::weakly_canonical(packageDir, rootError);
        if (rootError || !std::filesystem::is_directory(packageRoot)) {
            report.add(Error, "package root is not a readable directory", packageDir.generic_string());
            return report;
        }
        const auto descPath = packageRoot / "desc.json";

        auto root = parseJsonFile(report, descPath);
        if (!root.isObject()) {
            return report;
        }
        const auto &desc = root.toObject();
        static const std::set<std::string> descAllowed = {
            "$version", "id", "version", "compatVersion", "name", "vendor", "copyright",
            "description", "readme", "url", "contributes", "dependencies",
        };
        validateExtraKeys(report, descPath, desc, descAllowed, "",
                          "Remove the key or move application-specific data into a documented extension file.");

        requireString(report, descPath, desc, "id", "", R"("my_voicebank")");
        requireString(report, descPath, desc, "version", "", R"("1.0.0")");
        optionalString(report, descPath, desc, "$version", "");
        optionalString(report, descPath, desc, "compatVersion", "");
        optionalDisplayText(report, descPath, desc, "name", "");
        optionalDisplayText(report, descPath, desc, "vendor", "");
        optionalDisplayText(report, descPath, desc, "copyright", "");
        optionalDisplayText(report, descPath, desc, "description", "");
        optionalString(report, descPath, desc, "url", "");

        const auto id = getString(desc, "id");
        if (!id.empty() && !validIdText(id)) {
            report.add(Error, "invalid package id", jsonPath(descPath, "id"), id,
                       R"(Do not use / \ [ ] : ; ' " or spaces in package id.)");
        }
        const auto ver = getString(desc, "version");
        if (!ver.empty() && !validVersionText(ver)) {
            report.add(Error, "invalid package version", jsonPath(descPath, "version"), ver,
                       R"(Use x.y, x.y.z, or x.y.z.w, for example "1.0.0".)");
        }
        if (const auto *readme = findKey(desc, "readme")) {
            validatePathValue(report, packageRoot, packageRoot, descPath, "readme", *readme, true, false);
        }

        std::vector<std::filesystem::path> inferenceFiles;
        std::vector<std::filesystem::path> singerFiles;
        if (const auto *contributes = findKey(desc, "contributes")) {
            if (!contributes->isObject()) {
                addTypeError(report, descPath, "contributes", *contributes, "object");
            } else {
                const auto &contrib = contributes->toObject();
                static const std::set<std::string> contribAllowed = {"inferences", "singers"};
                validateExtraKeys(report, descPath, contrib, contribAllowed, "contributes",
                                  "Only contributes.inferences and contributes.singers are standard here.");
                for (const auto &entry : {std::string("inferences"), std::string("singers")}) {
                    const auto *value = findKey(contrib, entry);
                    if (!value) {
                        continue;
                    }
                    if (!value->isArray()) {
                        addTypeError(report, descPath, "contributes/" + entry, *value, "array of string paths");
                        continue;
                    }
                    const auto &array = value->toArray();
                    for (size_t i = 0; i < array.size(); ++i) {
                        const auto ptr = stdc::formatN("contributes/%1/%2", entry, i);
                        validatePathValue(report, packageRoot, packageRoot, descPath, ptr, array[i], true, false);
                        if (array[i].isString()) {
                            auto result = PackagePathResolver::resolve(packageRoot, packageRoot, array[i].toString());
                            if (!result.hasValue()) {
                                continue;
                            }
                            auto resolved = result.value();
                            if (entry == "inferences") {
                                inferenceFiles.push_back(std::move(resolved));
                            } else {
                                singerFiles.push_back(std::move(resolved));
                            }
                        }
                    }
                }
            }
        }

        if (const auto *dependencies = findKey(desc, "dependencies")) {
            if (!dependencies->isArray()) {
                addTypeError(report, descPath, "dependencies", *dependencies, "array");
            } else {
                const auto &array = dependencies->toArray();
                for (size_t i = 0; i < array.size(); ++i) {
                    const auto ptr = stdc::formatN("dependencies/%1", i);
                    if (!array[i].isObject()) {
                        addTypeError(report, descPath, ptr, array[i], "object with id/version/required");
                        continue;
                    }
                    const auto &dep = array[i].toObject();
                    static const std::set<std::string> depAllowed = {"id", "version", "required"};
                    validateExtraKeys(report, descPath, dep, depAllowed, ptr,
                                      "Remove the key; dependency entries support id, version and required.");
                    requireString(report, descPath, dep, "id", ptr, R"("base_package")");
                    requireString(report, descPath, dep, "version", ptr, R"("1.0.0")");
                    if (const auto *required = findKey(dep, "required"); required && !required->isBool()) {
                        addTypeError(report, descPath, ptr + "/required", *required, "boolean");
                    }
                }
            }
        }

        std::set<std::string> inferenceIds;
        for (const auto &file : inferenceFiles) {
            auto id = validateInferenceConfig(report, packageRoot, file);
            if (!id.empty()) {
                inferenceIds.insert(std::move(id));
            }
        }
        for (const auto &file : singerFiles) {
            validateSingerConfig(report, packageRoot, file, inferenceIds);
        }

        if (singerFiles.empty()) {
            report.add(Warning, "package contributes no singers", jsonPath(descPath, "contributes/singers"),
                       "<missing or empty>", "Add contributes.singers if this dspk contains a voicebank.");
        }
        return report;
    }

}
