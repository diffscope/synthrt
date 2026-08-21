#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

#include <synthrt/Core/Support/JSON.h>

#include <diffsinger/Bank/PackageParser.h>
#include <diffsinger/Bank/PackagePathResolver.h>

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
                stdc::formatN(R"(%1: failed to open package manifest)",
                              stdc::path::to_utf8(path)),
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

    // 解析配置中的 name 字段（可为字符串或本地化对象）。
    // locale 非空时按传入的 UI 语言从本地化对象选取显示名；locale 为空时
    // 保持旧行为（优先 default/en，否则取第一个字符串键），保证既有无 locale
    // 调用方（dsinfer-cli、C ABI、单测）行为完全不变。
    //
    // 匹配器收口在此一处，供歌手名/语言名/音色名复用：
    //   1. 规范化（小写、_ -> -）后精确匹配；
    //   2. 语言主码匹配（如 "zh" 命中 zh-Hans/zh-Hant/zh_CN）：唯一候选直接用，
    //      多候选按简体/繁体脚本偏好选择；
    //   3. "_" 回退键；
    //   4. 名称对象第一个字符串键。
    static std::string nameField(const JsonObject &obj, const std::string &locale = {}) {
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

        auto normalizeLocaleKey = [](const std::string &key) {
            std::string result = key;
            std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            std::replace(result.begin(), result.end(), '_', '-');
            return result;
        };
        auto languagePart = [&normalizeLocaleKey](const std::string &key) {
            const auto normalized = normalizeLocaleKey(key);
            const auto dash = normalized.find('-');
            return dash == std::string::npos ? normalized : normalized.substr(0, dash);
        };

        // 无 locale：旧行为。
        if (locale.empty()) {
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

        // 1. 精确匹配（规范化后）。
        const std::string localeNorm = normalizeLocaleKey(locale);
        for (const auto &[key, value] : nameObj) {
            if (value.isString() && normalizeLocaleKey(key) == localeNorm) {
                return value.toString();
            }
        }

        // 2. 语言主码匹配。
        const std::string langPart = languagePart(locale);
        std::vector<std::pair<std::string, std::string>> candidates;  // (规范化键, 文本)
        for (const auto &[key, value] : nameObj) {
            if (value.isString() && languagePart(key) == langPart) {
                candidates.emplace_back(normalizeLocaleKey(key), value.toString());
            }
        }
        if (candidates.size() == 1) {
            return candidates.front().second;
        }
        if (!candidates.empty()) {
            // 同语言多候选（常见于 zh-Hans/zh-Hant）：按脚本偏好选择。
            const bool wantsSimplified =
                localeNorm.find("-hans") != std::string::npos ||
                localeNorm == "zh-cn" || localeNorm.find("-cn") != std::string::npos ||
                localeNorm.find("-sg") != std::string::npos;
            const bool wantsTraditional =
                localeNorm.find("-hant") != std::string::npos ||
                localeNorm.find("-tw") != std::string::npos ||
                localeNorm.find("-hk") != std::string::npos ||
                localeNorm.find("-mo") != std::string::npos;
            for (const auto &candidate : candidates) {
                if (wantsSimplified && candidate.first.find("-hans") != std::string::npos)
                    return candidate.second;
                if (wantsTraditional && candidate.first.find("-hant") != std::string::npos)
                    return candidate.second;
            }
            return candidates.front().second;
        }

        // 3. "_" 回退键。
        auto fallbackIt = nameObj.find("_");
        if (fallbackIt != nameObj.end() && fallbackIt->second.isString()) {
            return fallbackIt->second.toString();
        }

        // 4. 名称对象第一个字符串键。
        for (const auto &[_, value] : nameObj) {
            if (value.isString()) {
                return value.toString();
            }
        }
        return {};
    }

    static std::filesystem::path resolvePath(const std::filesystem::path &packageRoot,
                                             const std::filesystem::path &basePath,
                                             const std::string &value, Error &err,
                                             std::string *errorPointer = nullptr,
                                             std::string_view pointer = {}) {
        if (value.empty()) {
            return {};
        }
        auto result = PackagePathResolver::resolve(packageRoot, basePath, value);
        if (!result.hasValue()) {
            err = result.error();
            if (errorPointer) {
                *errorPointer = pointer;
            }
            return {};
        }
        return result.value();
    }

    static void addRelaxedDiagnostic(PackageManifest &manifest, const Error &error,
                                     const std::filesystem::path &file, std::string_view pointer) {
        auto diagnostic = error.diagnostic();
        // diagnostic.location 是类 JSON pointer 格式（RFC 6901），必须使用
        // 正斜杠分隔符。此处不能用 stdc::path::to_utf8（Windows 返回反斜杠
        // 原生格式），而应使用 generic_string() 保证跨平台一致的正斜杠格式。
        // CODING-03 针对 human-readable error message，不适用于 machine-parseable
        // location 字段。
        diagnostic.location = stdc::formatN("%1#/%2", file.generic_string(), pointer);
        manifest.addDiagnostic(std::move(diagnostic));
    }


    static LanguageInfo parseLanguageObject(const JsonObject &obj,
                                             const std::filesystem::path &packageRoot,
                                             const std::filesystem::path &basePath,
                                             const std::string &locale, Error &err,
                                             std::string &errorPointer) {
        LanguageInfo lang;

        auto languageId = stringField(obj, "languageId");
        if (languageId.empty()) {
            languageId = stringField(obj, "id");
        }
        lang.setLanguageId(std::move(languageId));
        lang.setName(nameField(obj, locale));

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
            lang.setG2pPackageVersion(stdc::VersionNumber::fromString(g2pPackageVersion).value_or(stdc::VersionNumber()));
        }

        lang.setDict(resolvePath(packageRoot, basePath, stringField(obj, "dict"), err,
                                 &errorPointer, "configuration/languages/dict"));
        lang.setS2pMode(stringField(obj, "s2pMode"));
        lang.setOnsetMode(stringField(obj, "onsetMode"));
        lang.setS2pFile(resolvePath(packageRoot, basePath, stringField(obj, "s2pFile"), err,
                                    &errorPointer, "configuration/languages/s2pFile"));
        lang.setOnsetFile(resolvePath(packageRoot, basePath, stringField(obj, "onsetFile"), err,
                                      &errorPointer, "configuration/languages/onsetFile"));

        auto packagesIt = obj.find("g2pPackages");
        if (packagesIt != obj.end()) {
            std::vector<std::filesystem::path> packages;
            if (packagesIt->second.isString()) {
                packages.emplace_back(resolvePath(packageRoot, basePath, packagesIt->second.toString(), err,
                                                  &errorPointer, "configuration/languages/g2pPackages"));
            } else if (packagesIt->second.isArray()) {
                for (const auto &item : packagesIt->second.toArray()) {
                    if (item.isString()) {
                        packages.emplace_back(resolvePath(packageRoot, basePath, item.toString(), err,
                                                          &errorPointer, "configuration/languages/g2pPackages"));
                    }
                }
            }
            lang.setG2pPackages(std::move(packages));
        }

        return lang;
    }

    // Parse a singer config file. On read/parse failure, \p err is set and the
    // returned SingerManifest has an empty singerId (so callers can skip it in
    // Relaxed mode or report the error in Strict mode).
    static void parseSingerConfig(const std::filesystem::path &packageRoot,
                                  const std::filesystem::path &filePath,
                                  const std::string &locale, SingerManifest &singer,
                                  std::vector<LanguageInfo> &languages, Error &err,
                                  std::string &errorPointer) {
        Error readErr;
        auto text = readAll(filePath, readErr);
        if (readErr.type() != Error::NoError) {
            err = Error{
                srt::core::ErrorCode::PackageManifestInvalid,
                stdc::formatN(R"(%1: failed to read singer config: %2)",
                              stdc::path::to_utf8(filePath), readErr.message()),
            };
            return;
        }
        std::string parseErr;
        auto root = JsonValue::fromJson(text, true, &parseErr);
        if (!parseErr.empty() || !root.isObject()) {
            err = Error{
                srt::core::ErrorCode::PackageManifestInvalid,
                stdc::formatN(R"(%1: invalid singer config format: %2)",
                              stdc::path::to_utf8(filePath), parseErr),
            };
            return;
        }
        const auto &obj = root.toObject();
        auto singerId = stringField(obj, "id");
        if (!singerId.empty()) {
            singer.setSingerId(std::move(singerId));
        }
        singer.setName(nameField(obj, locale));
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
                    auto lang = parseLanguageObject(item.toObject(), packageRoot,
                                                    filePath.parent_path(), locale, err,
                                                    errorPointer);
                    if (err.type() != Error::NoError) {
                        return;
                    }
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
                        auto name = nameField(spkObj, locale);
                        if (!id.empty()) {
                            SpeakerInfo spk(std::move(id), std::move(name),
                                            singer.singerId());
                            // Parse toneRange as {min, max} MIDI note numbers.
                            // Accepts "toneRange": [min, max] or toneMin/toneMax
                            // pair. Format errors are silently skipped
                            // (toneRange stays nullopt); loading is never blocked.
                            if (const auto trIt = spkObj.find("toneRange");
                                trIt != spkObj.end() && trIt->second.isArray()) {
                                const auto &arr = trIt->second.toArray();
                                if (arr.size() == 2 && arr[0].isInt() && arr[1].isInt()) {
                                    const auto lo = static_cast<int>(arr[0].toInt());
                                    const auto hi = static_cast<int>(arr[1].toInt());
                                    // MIDI note numbers are 0-127; reject negative
                                    // values as format errors (silently skipped).
                                    if (lo >= 0 && lo <= hi) {
                                        spk.setToneRange(std::make_pair(lo, hi));
                                    }
                                }
                            } else {
                                const auto tMin = spkObj.find("toneMin");
                                const auto tMax = spkObj.find("toneMax");
                                if (tMin != spkObj.end() && tMin->second.isInt() &&
                                    tMax != spkObj.end() && tMax->second.isInt()) {
                                    const auto lo = static_cast<int>(tMin->second.toInt());
                                    const auto hi = static_cast<int>(tMax->second.toInt());
                                    if (lo >= 0 && lo <= hi) {
                                        spk.setToneRange(std::make_pair(lo, hi));
                                    }
                                }
                            }
                            speakers.emplace_back(std::move(spk));
                        }
                    }
                }
                singer.setSpeakers(std::move(speakers));
            }
        }
    }

    static InferenceInfo parseInferenceConfig(const std::filesystem::path &packageRoot,
                                               const std::filesystem::path &filePath, Error &err,
                                               std::string &errorPointer) {
        InferenceInfo info;
        info.configPath = filePath.lexically_normal();
        Error readErr;
        auto text = readAll(filePath, readErr);
        if (readErr.type() != Error::NoError) {
            err = Error{
                srt::core::ErrorCode::PackageManifestInvalid,
                stdc::formatN(R"(%1: failed to read inference config: %2)",
                              stdc::path::to_utf8(filePath), readErr.message()),
            };
            return info;
        }
        std::string parseErr;
        auto root = JsonValue::fromJson(text, true, &parseErr);
        if (!parseErr.empty() || !root.isObject()) {
            err = Error{
                srt::core::ErrorCode::PackageManifestInvalid,
                stdc::formatN(R"(%1: invalid inference config format: %2)",
                              stdc::path::to_utf8(filePath), parseErr),
            };
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
                    const auto resolved = resolvePath(packageRoot, filePath.parent_path(), value.toString(), err,
                                                      &errorPointer, "configuration/" + key);
                    if (err.type() != Error::NoError) {
                        return info;
                    }
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
                            const auto resolved = resolvePath(packageRoot, filePath.parent_path(), emb.toString(), err,
                                                              &errorPointer, "configuration/speakers/" + speakerId);
                            if (err.type() != Error::NoError) {
                                return info;
                            }
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

    void PackageParser::setDisplayLocale(std::string locale) {
        m_displayLocale = std::move(locale);
    }

    Expected<PackageManifest> PackageParser::parsePackage(const std::filesystem::path &packageDir,
                                                          ParseMode mode) const {
        std::error_code rootError;
        const auto packageRoot = std::filesystem::weakly_canonical(packageDir, rootError);
        if (rootError || !std::filesystem::is_directory(packageRoot)) {
            return Error{srt::core::ErrorCode::PackageManifestInvalid,
                         stdc::formatN(R"(%1: package root is not a readable directory)",
                                       stdc::path::to_utf8(packageDir))};
        }
        const auto manifestPath = packageRoot / "desc.json";

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
                stdc::formatN(R"(%1: invalid package manifest format: %2)",
                              stdc::path::to_utf8(manifestPath), parseErr),
            };
        }
        if (!root.isObject()) {
            return Error{
                srt::core::ErrorCode::PackageManifestInvalid,
                stdc::formatN(R"(%1: package manifest must be a JSON object)",
                              stdc::path::to_utf8(manifestPath)),
            };
        }
        const auto &obj = root.toObject();

        PackageManifest info;
        info.setRootPath(packageRoot);
        {
            auto it = obj.find("id");
            if (it == obj.end() || !it->second.isString()) {
                return Error{
                    srt::core::ErrorCode::PackageManifestMissingField,
                    stdc::formatN(R"(%1: missing required field "id")",
                                  stdc::path::to_utf8(manifestPath)),
                };
            } else {
                info.setPackageId(it->second.toString());
            }
        }

        // version
        {
            auto it = obj.find("version");
            if (it != obj.end() && it->second.isString()) {
                info.setVersion(stdc::VersionNumber::fromString(it->second.toString()).value_or(stdc::VersionNumber()));
            } else {
                return Error::packageError(
                    srt::core::ErrorCode::PackageManifestMissingField,
                    stdc::formatN(R"(%1: missing required field "version")",
                                  stdc::path::to_utf8(manifestPath)),
                    info.packageId());
            }
        }

        // compatVersion (optional)
        {
            auto it = obj.find("compatVersion");
            if (it != obj.end() && it->second.isString()) {
                info.setCompatVersion(stdc::VersionNumber::fromString(it->second.toString()).value_or(stdc::VersionNumber()));
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
            auto parseRefs = [&](const char *key) -> Expected<std::vector<std::filesystem::path>> {
                std::vector<std::filesystem::path> refs;
                if (auto refIt = contrib.find(key); refIt != contrib.end() && refIt->second.isArray()) {
                    const auto &items = refIt->second.toArray();
                    for (size_t index = 0; index < items.size(); ++index) {
                        const auto &item = items[index];
                        if (item.isString()) {
                            Error pathError;
                            auto path = resolvePath(packageRoot, packageRoot, item.toString(), pathError);
                            if (pathError.type() != Error::NoError) {
                                if (mode == ParseMode::Relaxed) {
                                    addRelaxedDiagnostic(
                                        info, pathError, manifestPath,
                                        stdc::formatN("contributes/%1/%2", key, index));
                                    continue;
                                }
                                return pathError;
                            }
                            refs.emplace_back(std::move(path));
                        }
                    }
                }
                return refs;
            };
            auto singerRefsResult = parseRefs("singers");
            if (!singerRefsResult.hasValue()) {
                return singerRefsResult.error();
            }
            auto inferenceRefsResult = parseRefs("inferences");
            if (!inferenceRefsResult.hasValue()) {
                return inferenceRefsResult.error();
            }
            auto singerRefs = singerRefsResult.take();
            auto inferenceRefs = inferenceRefsResult.take();
            info.setSingerRefs(singerRefs);
            info.setInferenceRefs(std::move(inferenceRefs));

            std::vector<InferenceInfo> standardInferences;
            for (const auto &ref : info.inferenceRefs()) {
                Error cfgErr;
                std::string errorPointer;
                auto inference = parseInferenceConfig(packageRoot, ref, cfgErr, errorPointer);
                if (cfgErr.type() != Error::NoError) {
                    // BF-33: Strict mode must report corrupted/missing inference
                    // configs instead of silently skipping them. Relaxed mode
                    // keeps the legacy tolerant behavior.
                    if (mode == ParseMode::Strict) {
                        return cfgErr;
                    }
                    addRelaxedDiagnostic(info, cfgErr, ref, errorPointer);
                    continue;
                }
                if (!inference.id.empty()) {
                    // Stamp the owning package's identity so downstream
                    // ModelRegistry/SpeakerMapper can isolate inferences
                    // that share the same id across different packages
                    // (ARCH-06). Mirrors the SingerManifest packageId stamp
                    // applied below for singers.
                    inference.packageId = info.packageId();
                    standardInferences.emplace_back(std::move(inference));
                }
            }
            info.setInferences(std::move(standardInferences));

            std::vector<LanguageInfo> standardLanguages;
            std::vector<SingerManifest> standardSingers;
            for (const auto &ref : singerRefs) {
                SingerManifest singer;
                Error cfgErr;
                std::string errorPointer;
                parseSingerConfig(packageRoot, ref, m_displayLocale, singer, standardLanguages,
                                  cfgErr, errorPointer);
                if (cfgErr.type() != Error::NoError) {
                    // BF-33: Strict mode must report corrupted/missing singer
                    // configs instead of silently skipping them. Relaxed mode
                    // keeps the legacy tolerant behavior.
                    if (mode == ParseMode::Strict) {
                        return cfgErr;
                    }
                    addRelaxedDiagnostic(info, cfgErr, ref, errorPointer);
                    continue;
                }
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
