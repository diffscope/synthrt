#include "BundleLoader.h"

#include <fstream>
#include <sstream>

#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

namespace srt::g2p::plugins::Multig2p {

    // ============== VocabularyData ==============

    int VocabularyData::lookup(const std::string &symbol) const {
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (symbols[i] == symbol) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    const std::string &VocabularyData::symbolAt(int id) const {
        static const std::string empty;
        if (id < 0 || id >= static_cast<int>(symbols.size())) {
            return empty;
        }
        return symbols[static_cast<size_t>(id)];
    }

    std::string VocabularyData::phonemeAt(int id) const {
        const auto &sym = symbolAt(id);
        if (sym.empty()) {
            return {};
        }
        const auto pos = sym.rfind('/');
        if (pos == std::string::npos) {
            return sym;
        }
        return sym.substr(pos + 1);
    }

    std::string VocabularyData::computeHash() const {
        // 与 MultiG2p Vocabulary.hash() 一致：SHA-256 前 16 hex，符号间用 \x1f 分隔。
        // synthrt 不提供 SHA-256 工具，此处返回空字符串；调用方优先用 vocabulary.json
        // 的 vocab_hash 字段做交叉校验（BundleLoader::loadVocabulary 已读取）。
        // 如需精确校验，可引入第三方加密库或调用 Python 预计算结果。
        return {};
    }

    // ============== BundleLoader ==============

    srt::core::Expected<srt::core::JsonValue>
    BundleLoader::readJsonFile(const std::filesystem::path &path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return srt::g2p::Error(
                srt::g2p::Error::FileSystemError,
                stdc::formatN(R"(failed to open "%1")", stdc::path::to_utf8(path)));
        }
        std::stringstream ss;
        ss << file.rdbuf();
        std::string errString;
        const auto j = srt::core::JsonValue::fromJson(ss.str(), true, &errString);
        if (!errString.empty()) {
            return srt::g2p::Error(
                srt::g2p::Error::ConfigError,
                stdc::formatN(R"(JSON parse error in "%1": %2)",
                              stdc::path::to_utf8(path), errString));
        }
        return j;
    }

    srt::core::Expected<BundleMeta>
    BundleLoader::loadBundleJson(const std::filesystem::path &bundlePath) {
        auto jsonExp = readJsonFile(bundlePath);
        if (!jsonExp) {
            return jsonExp.takeError();
        }
        const auto j = jsonExp.take();
        if (!j.isObject()) {
            return srt::g2p::Error(
                srt::g2p::Error::ConfigError,
                stdc::formatN(R"(bundle.json is not a JSON object: "%1")",
                              stdc::path::to_utf8(bundlePath)));
        }
        const auto &obj = j.toObject();

        BundleMeta meta;
        auto getStr = [&obj](const std::string &key) -> std::string {
            const auto it = obj.find(key);
            return (it != obj.end() && it->second.isString()) ? it->second.toString() : std::string{};
        };

        meta.bundleVersion = getStr("bundle_version");
        meta.schemaVersion = getStr("schema_version");
        meta.modelVersion = getStr("model_version");
        meta.minRuntimeVersion = getStr("min_runtime_version");
        meta.vocabHash = getStr("vocab_hash");
        meta.generatedAt = getStr("generated_at");

        // opset_version（int）
        if (const auto it = obj.find("opset_version"); it != obj.end() && it->second.isInt()) {
            meta.opsetVersion = static_cast<int>(it->second.toInt());
        }

        // export_flags（array of string）
        if (const auto it = obj.find("export_flags"); it != obj.end() && it->second.isArray()) {
            for (const auto &v : it->second.toArray()) {
                if (v.isString()) {
                    meta.exportFlags.push_back(v.toString());
                }
            }
        }

        // files（object: logical_name -> filename）
        if (const auto it = obj.find("files"); it != obj.end() && it->second.isObject()) {
            for (const auto &[k, v] : it->second.toObject()) {
                if (v.isString()) {
                    meta.files[k] = v.toString();
                }
            }
        }

        // languages（array of string）
        if (const auto it = obj.find("languages"); it != obj.end() && it->second.isArray()) {
            for (const auto &v : it->second.toArray()) {
                if (v.isString()) {
                    meta.languages.push_back(v.toString());
                }
            }
        }

        if (meta.bundleVersion.empty()) {
            return srt::g2p::Error(
                srt::g2p::Error::ConfigError,
                "bundle.json missing bundle_version field");
        }
        if (meta.languages.empty()) {
            return srt::g2p::Error(
                srt::g2p::Error::ConfigError,
                "bundle.json missing or empty languages field");
        }
        return meta;
    }

    srt::core::Expected<VocabularyData>
    BundleLoader::loadVocabulary(const std::filesystem::path &vocabPath) {
        auto jsonExp = readJsonFile(vocabPath);
        if (!jsonExp) {
            return jsonExp.takeError();
        }
        const auto j = jsonExp.take();
        if (!j.isObject()) {
            return srt::g2p::Error(
                srt::g2p::Error::ConfigError,
                stdc::formatN(R"(vocabulary.json is not a JSON object: "%1")",
                              stdc::path::to_utf8(vocabPath)));
        }
        const auto &obj = j.toObject();

        VocabularyData vocab;

        // symbols（array of string）
        if (const auto it = obj.find("symbols"); it != obj.end() && it->second.isArray()) {
            for (const auto &v : it->second.toArray()) {
                if (v.isString()) {
                    vocab.symbols.push_back(v.toString());
                }
            }
        } else {
            return srt::g2p::Error(
                srt::g2p::Error::ConfigError,
                "vocabulary.json missing symbols array");
        }

        // global_symbols（array of string）
        if (const auto it = obj.find("global_symbols"); it != obj.end() && it->second.isArray()) {
            for (const auto &v : it->second.toArray()) {
                if (v.isString()) {
                    vocab.globalSymbols.push_back(v.toString());
                }
            }
        }

        // vocab_hash（string）
        if (const auto it = obj.find("vocab_hash"); it != obj.end() && it->second.isString()) {
            vocab.vocabHash = it->second.toString();
        }

        // 便捷索引（约定顺序：<unk> <pad> <bos> <eos>）
        auto findGlobal = [&vocab](const std::string &name) -> int {
            for (size_t i = 0; i < vocab.globalSymbols.size(); ++i) {
                if (vocab.globalSymbols[i] == name) {
                    // global_symbols 在 symbols 中的前 N 项
                    return static_cast<int>(i);
                }
            }
            return -1;
        };
        vocab.unkIdx = findGlobal("<unk>");
        vocab.padIdx = findGlobal("<pad>");
        vocab.bosIdx = findGlobal("<bos>");
        vocab.eosIdx = findGlobal("<eos>");
        if (vocab.unkIdx < 0) vocab.unkIdx = 0;
        if (vocab.padIdx < 0) vocab.padIdx = 1;
        if (vocab.bosIdx < 0) vocab.bosIdx = 2;
        if (vocab.eosIdx < 0) vocab.eosIdx = 3;

        return vocab;
    }

    std::optional<std::filesystem::path>
    BundleLoader::resolveOnnxFile(const BundleMeta &meta,
                                  const std::filesystem::path &bundleDir,
                                  const std::string &logicalName) {
        const auto it = meta.files.find(logicalName);
        if (it == meta.files.end()) {
            return std::nullopt;
        }
        return bundleDir / it->second;
    }

}
