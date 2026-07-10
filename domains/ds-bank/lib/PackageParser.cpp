#include <fstream>
#include <sstream>

#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

#include <synthrt/Core/Support/JSON.h>

#include <diffsinger/Bank/PackageParser.h>

using srt::core::Error;
using srt::core::Expected;
using srt::core::JsonArray;
using srt::core::JsonObject;
using srt::core::JsonValue;

namespace ds::bank {

    static inline std::string readAll(const std::filesystem::path &path, Error &err) {
        std::ifstream file(path);
        if (!file.is_open()) {
            err = Error{
                Error::FileNotOpen,
                stdc::formatN(R"(%1: failed to open package manifest)", path),
            };
            return {};
        }
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    static std::string stringField(const JsonObject &obj, const std::string &key) {
        const auto it = obj.find(key);
        if (it != obj.end() && it->second.isString()) {
            return it->second.toString();
        }
        return {};
    }

    static std::string nameField(const JsonObject &obj) {
        const auto it = obj.find("name");
        if (it == obj.end()) {
            return {};
        }
        if (it->second.isString()) {
            return it->second.toString();
        }
        if (!it->second.isObject()) {
            return {};
        }

        const auto &nameObj = it->second.toObject();
        for (const auto &key : {std::string("default"), std::string("en")}) {
            auto nameIt = nameObj.find(key);
            if (nameIt != nameObj.end() && nameIt->second.isString()) {
                return nameIt->second.toString();
            }
        }
        for (const auto &[_, value] : nameObj) {
            if (value.isString()) {
                return value.toString();
            }
        }
        return {};
    }

    static std::filesystem::path resolvePath(const std::filesystem::path &basePath,
                                             const std::string &value) {
        if (value.empty()) {
            return {};
        }
        const std::filesystem::path path(value);
        if (path.is_absolute()) {
            return path.lexically_normal();
        }
        return (basePath / path).lexically_normal();
    }

    static LanguageInfo parseLanguageObject(const JsonObject &obj,
                                            const std::filesystem::path &basePath) {
        LanguageInfo lang;

        auto languageId = stringField(obj, "languageId");
        if (languageId.empty()) {
            languageId = stringField(obj, "id");
        }
        lang.setLanguageId(std::move(languageId));
        lang.setName(nameField(obj));

        auto g2pId = stringField(obj, "g2pId");
        if (g2pId.empty()) {
            g2pId = stringField(obj, "g2p");
        }
        lang.setG2pId(std::move(g2pId));

        auto g2pVersion = stringField(obj, "g2pVersion");
        if (!g2pVersion.empty()) {
            lang.setG2pVersion(std::move(g2pVersion));
        }

        auto g2pPackageVersion = stringField(obj, "g2pPackageVersion");
        if (!g2pPackageVersion.empty()) {
            lang.setG2pPackageVersion(stdc::VersionNumber::fromString(g2pPackageVersion));
        }

        lang.setDict(resolvePath(basePath, stringField(obj, "dict")));
        lang.setS2pMode(stringField(obj, "s2pMode"));
        lang.setOnsetMode(stringField(obj, "onsetMode"));
        lang.setS2pFile(resolvePath(basePath, stringField(obj, "s2pFile")));
        lang.setOnsetFile(resolvePath(basePath, stringField(obj, "onsetFile")));

        auto packagesIt = obj.find("g2pPackages");
        if (packagesIt != obj.end()) {
            std::vector<std::filesystem::path> packages;
            if (packagesIt->second.isString()) {
                packages.emplace_back(resolvePath(basePath, packagesIt->second.toString()));
            } else if (packagesIt->second.isArray()) {
                for (const auto &item : packagesIt->second.toArray()) {
                    if (item.isString()) {
                        packages.emplace_back(resolvePath(basePath, item.toString()));
                    }
                }
            }
            lang.setG2pPackages(std::move(packages));
        }

        return lang;
    }

    static void parseSingerConfig(const std::filesystem::path &filePath, SingerManifest &singer,
                                 std::vector<LanguageInfo> &languages) {
        Error readErr;
        auto text = readAll(filePath, readErr);
        if (readErr.type() != Error::NoError) {
            return;
        }
        std::string parseErr;
        auto root = JsonValue::fromJson(text, true, &parseErr);
        if (!parseErr.empty() || !root.isObject()) {
            return;
        }
        const auto &obj = root.toObject();
        auto singerId = stringField(obj, "id");
        if (!singerId.empty()) {
            singer.setSingerId(std::move(singerId));
        }
        singer.setName(nameField(obj));
        if (const auto it = obj.find("imports"); it != obj.end() && it->second.isArray()) {
            std::vector<SingerImportInfo> imports;
            for (const auto &item : it->second.toArray()) {
                if (!item.isObject()) {
                    continue;
                }
                const auto &importObj = item.toObject();
                SingerImportInfo import;
                import.inferenceId = stringField(importObj, "id");
                if (import.inferenceId.empty()) {
                    import.inferenceId = stringField(importObj, "inferenceId");
                }
                if (const auto optionsIt = importObj.find("options");
                    optionsIt != importObj.end() && optionsIt->second.isObject()) {
                    const auto &options = optionsIt->second.toObject();
                    if (const auto mappingIt = options.find("speakerMapping");
                        mappingIt != options.end() && mappingIt->second.isObject()) {
                        for (const auto &[from, to] : mappingIt->second.toObject()) {
                            if (to.isString()) {
                                import.speakerMapping.emplace(from, to.toString());
                            }
                        }
                    }
                }
                if (!import.inferenceId.empty()) {
                    imports.emplace_back(std::move(import));
                }
            }
            singer.setImports(std::move(imports));
        }
        if (const auto *config = [&]() -> const JsonValue * {
                auto it = obj.find("configuration");
                return it == obj.end() ? nullptr : &it->second;
            }(); config && config->isObject()) {
            const auto &cfg = config->toObject();
            const auto defaultLanguage = stringField(cfg, "defaultLanguage");
            singer.setDefaultLanguage(defaultLanguage);
            if (const auto it = cfg.find("languages"); it != cfg.end() && it->second.isArray()) {
                std::vector<LanguageInfo> langs;
                for (const auto &item : it->second.toArray()) {
                    if (!item.isObject()) {
                        continue;
                    }
                    auto lang = parseLanguageObject(item.toObject(), filePath.parent_path());
                    if (!lang.languageId().empty()) {
                        langs.emplace_back(lang);
                    }
                    languages.emplace_back(std::move(lang));
                }
                singer.setLanguages(std::move(langs));
            }
            if (const auto it = cfg.find("speakers"); it != cfg.end() && it->second.isArray()) {
                std::vector<SpeakerInfo> speakers;
                for (const auto &item : it->second.toArray()) {
                    if (item.isObject()) {
                        const auto &spkObj = item.toObject();
                        auto id = stringField(spkObj, "id");
                        auto name = nameField(spkObj);
                        if (!id.empty()) {
                            speakers.emplace_back(std::move(id), std::move(name),
                                                  singer.singerId());
                        }
                    }
                }
                singer.setSpeakers(std::move(speakers));
            }
        }
    }

    static InferenceInfo parseInferenceConfig(const std::filesystem::path &filePath) {
        InferenceInfo info;
        info.configPath = filePath.lexically_normal();
        Error readErr;
        auto text = readAll(filePath, readErr);
        if (readErr.type() != Error::NoError) {
            return info;
        }
        std::string parseErr;
        auto root = JsonValue::fromJson(text, true, &parseErr);
        if (!parseErr.empty() || !root.isObject()) {
            return info;
        }
        const auto &obj = root.toObject();
        info.id = stringField(obj, "id");
        info.className = stringField(obj, "class");
        if (const auto it = obj.find("level"); it != obj.end() && it->second.isInt()) {
            info.level = static_cast<int>(it->second.toInt());
        }
        if (const auto it = obj.find("configuration"); it != obj.end() && it->second.isObject()) {
            const auto &cfg = it->second.toObject();
            for (const auto &[key, value] : cfg) {
                const bool pathLike = key == "model" || key == "encoder" || key == "predictor" ||
                                      key == "phonemes" || key == "languages" ||
                                      (key.size() >= 5 && key.substr(key.size() - 5) == "Model");
                if (pathLike && value.isString()) {
                    const auto resolved = resolvePath(filePath.parent_path(), value.toString());
                    info.resourcePaths.emplace_back(stdc::path::to_utf8(resolved));
                    if (key == "phonemes") {
                        info.phonemesPath = resolved;
                    } else if (key == "languages") {
                        info.languagesPath = resolved;
                    } else {
                        info.modelPaths.emplace(key, resolved);
                    }
                }
                if (key == "speakers" && value.isObject()) {
                    for (const auto &[speakerId, emb] : value.toObject()) {
                        if (emb.isString()) {
                            const auto resolved = resolvePath(filePath.parent_path(), emb.toString());
                            info.resourcePaths.emplace_back(stdc::path::to_utf8(resolved));
                            info.speakerEmbeddings.emplace(speakerId, resolved);
                        }
                    }
                }
                if (key == "parameters" && value.isArray()) {
                    for (const auto &item : value.toArray()) {
                        if (item.isString()) {
                            info.parameters.emplace_back(item.toString());
                        }
                    }
                }
                if (key == "hiddenSize" && value.isInt()) {
                    info.hiddenSize = static_cast<int>(value.toInt());
                } else if (key == "sampleRate" && value.isInt()) {
                    info.sampleRate = static_cast<int>(value.toInt());
                } else if (key == "hopSize" && value.isInt()) {
                    info.hopSize = static_cast<int>(value.toInt());
                } else if (key == "frameWidth" && value.isDouble()) {
                    info.frameWidth = value.toDouble();
                } else if (key == "useLanguageId" && value.isBool()) {
                    info.useLanguageId = value.toBool();
                } else if (key == "useSpeakerEmbedding" && value.isBool()) {
                    info.useSpeakerEmbedding = value.toBool();
                } else if (key == "useContinuousAcceleration" && value.isBool()) {
                    info.useContinuousAcceleration = value.toBool();
                }
            }
        }
        return info;
    }

    Expected<PackageManifest> PackageParser::parsePackage(const std::filesystem::path &packageDir,
                                                          ParseMode mode) const {
        const auto manifestPath = packageDir / "desc.json";

        // Read manifest text
        Error readErr;
        auto text = readAll(manifestPath, readErr);
        if (readErr.type() != Error::NoError) {
            return readErr;
        }

        // Parse JSON
        std::string parseErr;
        auto root = JsonValue::fromJson(text, true, &parseErr);
        if (!parseErr.empty()) {
            return Error{
                srt::core::ErrorCode::PackageManifestInvalid,
                stdc::formatN(R"(%1: invalid package manifest format: %2)", manifestPath, parseErr),
            };
        }
        if (!root.isObject()) {
            return Error{
                srt::core::ErrorCode::PackageManifestInvalid,
                stdc::formatN(R"(%1: package manifest must be a JSON object)", manifestPath),
            };
        }
        const auto &obj = root.toObject();

        PackageManifest info;
        info.setRootPath(packageDir.lexically_normal());
        {
            auto it = obj.find("id");
            if (it == obj.end() || !it->second.isString()) {
                (void) mode;
                return Error{
                    srt::core::ErrorCode::PackageManifestMissingField,
                    stdc::formatN(R"(%1: missing required field "id")", manifestPath),
                };
            } else {
                info.setPackageId(it->second.toString());
            }
        }

        // version
        {
            auto it = obj.find("version");
            if (it != obj.end() && it->second.isString()) {
                info.setVersion(stdc::VersionNumber::fromString(it->second.toString()));
            } else {
                return Error::packageError(
                    srt::core::ErrorCode::PackageManifestMissingField,
                    stdc::formatN(R"(%1: missing required field "version")", manifestPath),
                    info.packageId());
            }
        }

        // compatVersion (optional)
        {
            auto it = obj.find("compatVersion");
            if (it != obj.end() && it->second.isString()) {
                info.setCompatVersion(stdc::VersionNumber::fromString(it->second.toString()));
            }
        }

        // name
        {
            info.setName(nameField(obj));
        }

        // description
        {
            auto it = obj.find("description");
            if (it != obj.end() && it->second.isString()) {
                info.setDescription(it->second.toString());
            }
        }

        // author
        {
            auto it = obj.find("vendor");
            if (it != obj.end() && it->second.isString()) {
                info.setAuthor(it->second.toString());
            }
        }

        // license
        {
            auto it = obj.find("license");
            if (it != obj.end() && it->second.isString()) {
                info.setLicense(it->second.toString());
            }
        }

        {
            auto it = obj.find("dependencies");
            if (it != obj.end() && it->second.isArray()) {
                std::vector<std::string> deps;
                for (const auto &dep : it->second.toArray()) {
                    if (dep.isString()) {
                        deps.emplace_back(dep.toString());
                    } else if (dep.isObject()) {
                        auto id = stringField(dep.toObject(), "id");
                        if (!id.empty()) {
                            deps.emplace_back(std::move(id));
                        }
                    }
                }
                info.setDependencies(std::move(deps));
            }
        }

        if (auto it = obj.find("contributes"); it != obj.end() && it->second.isObject()) {
            const auto &contrib = it->second.toObject();
            auto parseRefs = [&](const char *key) {
                std::vector<std::filesystem::path> refs;
                if (auto refIt = contrib.find(key); refIt != contrib.end() && refIt->second.isArray()) {
                    for (const auto &item : refIt->second.toArray()) {
                        if (item.isString()) {
                            refs.emplace_back(resolvePath(packageDir, item.toString()));
                        }
                    }
                }
                return refs;
            };
            auto singerRefs = parseRefs("singers");
            auto inferenceRefs = parseRefs("inferences");
            info.setSingerRefs(singerRefs);
            info.setInferenceRefs(std::move(inferenceRefs));

            std::vector<InferenceInfo> standardInferences;
            for (const auto &ref : info.inferenceRefs()) {
                auto inference = parseInferenceConfig(ref);
                if (!inference.id.empty()) {
                    standardInferences.emplace_back(std::move(inference));
                }
            }
            info.setInferences(std::move(standardInferences));

            std::vector<LanguageInfo> standardLanguages;
            std::vector<SingerManifest> standardSingers;
            for (const auto &ref : singerRefs) {
                SingerManifest singer;
                parseSingerConfig(ref, singer, standardLanguages);
                if (!singer.singerId().empty()) {
                    singer.setPackageId(info.packageId());
                    singer.setPackageVersion(info.version());
                    standardSingers.emplace_back(std::move(singer));
                }
            }
            info.setSingers(std::move(standardSingers));
            info.setLanguages(std::move(standardLanguages));
        }

        return info;
    }

}
