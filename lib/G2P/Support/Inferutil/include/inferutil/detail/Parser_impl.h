// DO NOT include this file directly.
// Include <InferUtil/Parser.h> instead.

#pragma once

#ifndef SRT_G2P_PLUGINS_INFERUTIL_PARSER_H
#error "Parser_impl.h should only be included by Parser.h"
#endif

#include <fstream>
#include <utility>

#include <synthrt/Core/Support/JSON.h>
#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

namespace srt::g2p::plugins::InferUtil
{
    inline void ConfigurationParser::parse_bool_optional(bool &out, const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (it->second.isBool()) {
                out = it->second.toBool();
            } else {
                collectError("boolean field \"" + fieldName + "\" type mismatch");
            }
        } else {
            // Nothing to do
        }
    }

    inline void ConfigurationParser::parse_int_optional(int &out, const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (it->second.isNumber()) {
                out = static_cast<int>(it->second.toInt());
            } else {
                collectError("integer field \"" + fieldName + "\" type mismatch");
            }
        } else {
            // Nothing to do
        }
    }

    inline void ConfigurationParser::parse_positive_int_optional(int &out, const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (it->second.isNumber()) {
                if (const auto val = static_cast<int>(it->second.toInt()); val > 0) {
                    out = val;
                } else {
                    collectError("integer field \"" + fieldName + "\" must be positive");
                }
            } else {
                collectError("integer field \"" + fieldName + "\" type mismatch");
            }
        } else {
            // Nothing to do
        }
    }
    inline void ConfigurationParser::parse_double_optional(double &out, const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (it->second.isNumber()) {
                out = it->second.toDouble();
            } else {
                collectError("float field \"" + fieldName + "\" type mismatch");
            }
        } else {
            // Nothing to do
        }
    }

    inline void ConfigurationParser::parse_positive_double_optional(double &out, const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (it->second.isNumber()) {
                if (const auto val = it->second.toDouble(); val > 0) {
                    out = val;
                } else {
                    collectError("float field \"" + fieldName + "\" must be positive");
                }
            } else {
                collectError("float field \"" + fieldName + "\" type mismatch");
            }
        } else {
            // Nothing to do
        }
    }

    inline void ConfigurationParser::parse_string_required(std::string &out, const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (it->second.isString()) {
                out = it->second.toString();
            } else {
                collectError("string field \"" + fieldName + "\" type mismatch");
            }
        } else {
            collectError("string field \"" + fieldName + "\" is missing");
        }
    }

    inline void ConfigurationParser::parse_path_required(std::filesystem::path &out, const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (!it->second.isString()) {
                collectError("string field \"" + fieldName + "\" type mismatch");
            } else {
                out = stdc::path::clean_path(spec->path() / stdc::path::from_utf8(it->second.toStringView()));
            }
        } else {
            collectError("string field \"" + fieldName + "\" is missing");
        }
    }

    inline void ConfigurationParser::parse_phonemes(std::map<std::string, int> &out, const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (!it->second.isString()) {
                collectError(R"(string field "phonemes" type mismatch)");
            } else {
                const auto path = spec->path() / stdc::path::from_utf8(it->second.toStringView());
                loadIdMapping(it->first, path, out);
            }
        } else {
            collectError("string field \"phonemes\" is missing");
        }
    }

    inline void ConfigurationParser::parse_verify_required(std::vector<VerifyEntry> &out,
                                                           const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (!it->second.isArray()) {
                collectError("array field \"" + fieldName + "\" type mismatch");
            } else {
                const auto &arr = it->second.toArray();
                out.clear();
                out.reserve(arr.size());

                for (size_t i = 0; i < arr.size(); ++i) {
                    const auto &item = arr[i];
                    if (!item.isObject()) {
                        collectError("verify entry #" + std::to_string(i) + " must be an object");
                        continue;
                    }

                    const auto &obj = item.toObject();
                    VerifyEntry entry;

                    if (const auto typeIt = obj.find("type"); typeIt != obj.end()) {
                        if (typeIt->second.isString()) {
                            entry.type = typeIt->second.toString();
                        } else {
                            collectError("verify entry #" + std::to_string(i) + " field \"type\" must be string");
                            continue;
                        }
                    } else {
                        collectError("verify entry #" + std::to_string(i) + " missing required field \"type\"");
                        continue;
                    }

                    if (const auto valueIt = obj.find("value"); valueIt != obj.end()) {
                        const auto &valueArr = valueIt->second.toArray();
                        std::string combined;
                        for (size_t j = 0; j < valueArr.size(); ++j) {
                            if (valueArr[j].isString()) {
                                if (entry.type == "dict") {
                                    const auto path = spec->path() / stdc::path::from_utf8(valueArr[j].toString());
                                    entry.value.push_back(stdc::path::to_utf8(path));
                                } else
                                    entry.value.push_back(valueArr[j].toString());
                            } else
                                collectError("verify entry #" + std::to_string(i) + " array value #" +
                                             std::to_string(j) + " must be string");
                        }
                    } else {
                        collectError("verify entry #" + std::to_string(i) + " missing required field \"value\"");
                        continue;
                    }

                    if (const auto modeIt = obj.find("mode"); modeIt != obj.end()) {
                        if (modeIt->second.isString()) {
                            entry.mode = modeIt->second.toString();
                        } else {
                            collectError("verify entry #" + std::to_string(i) + " field \"mode\" must be string");
                            continue;
                        }
                    } else {
                        collectError("verify entry #" + std::to_string(i) + " missing required field \"mode\"");
                        continue;
                    }
                    out.push_back(std::move(entry));
                }
            }
        } else {
            collectError("array field \"" + fieldName + "\" is missing");
        }
    }

    inline void ConfigurationParser::parse_stringVec_required(std::vector<std::string> &out,
                                                              const std::string &fieldName) {
        const auto &config = *pConfig;

        if (const auto it = config.find(fieldName); it != config.end()) {
            if (!it->second.isArray()) {
                collectError("array field \"" + fieldName + "\" type mismatch");
            } else {
                const auto &arr = it->second.toArray();
                out.clear();
                out.reserve(arr.size());

                std::vector<std::string> regexes;
                for (size_t i = 0; i < arr.size(); ++i) {
                    if (const auto &item = arr[i]; item.isString())
                        regexes.push_back(item.toString());
                    else
                        collectError(fieldName + " array value #" + std::to_string(i) + " must be string");
                }
                out = regexes;
            }
        } else {
            collectError("array field \"" + fieldName + "\" is missing");
        }
    }

    inline bool ConfigurationParser::loadIdMapping(const std::string &fieldName, const std::filesystem::path &path,
                                                   std::map<std::string, int> &out) {
        std::ifstream file(path);
        if (!file.is_open()) {
            collectError(
                stdc::formatN(R"(error loading "%1": %2 file not found)", fieldName, stdc::path::to_utf8(path)));
            return false;
        }
        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        std::string buffer(size, '\0');
        file.seekg(0);
        file.read(buffer.data(), size);

        std::string errString;
        const auto j = srt::core::JsonValue::fromJson(buffer, true, &errString);
        if (!errString.empty()) {
            if (ec) {
                ec->collectError(std::move(errString));
            }
            return false;
        }

        if (!j.isObject()) {
            collectError(stdc::formatN(R"(error loading "%1": outer JSON is not an object)", fieldName));
            return false;
        }

        const auto &obj = j.toObject();
        bool flag = true;
        for (const auto &[key, value] : obj) {
            if (!value.isInt()) {
                flag = false;
                collectError(stdc::formatN(R"(error loading "%1": value of key "%2" is not int)", fieldName, key));
            } else {
                out[key] = static_cast<int>(value.toInt());
            }
        }
        return flag;
    }
} // namespace srt::g2p::plugins::InferUtil
