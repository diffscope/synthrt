#include <synthrt/S2P/RuleOnsetMarker.h>

#include <algorithm>
#include <cstddef>
#include <istream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace srt::s2p {

    namespace {

        constexpr auto AnyPhonemeType = "*";

        struct RuleTerm {
            std::string key;
            bool isWildcard{false};
        };

        struct QueryTerm {
            std::string specifiedKey;
            std::string wildcardKey;
        };

        std::runtime_error parseError(const std::string &reason) {
            return std::runtime_error("RuleOnsetMarker parse error: " + reason);
        }

    }

    class RuleOnsetMarker::Private {
    public:
        class TrieNode {
        public:
            void insert(const std::vector<RuleTerm> &pattern, const std::vector<int> &onsets, std::size_t offset = 0) {
                if (offset >= pattern.size()) {
                    value = onsets;
                    return;
                }

                const auto &term = pattern[offset];
                auto &children = term.isWildcard ? wildcards : exactChildren;
                auto &child = children[term.key];
                if (!child) {
                    child = std::make_unique<TrieNode>();
                }
                child->insert(pattern, onsets, offset + 1);
            }

            std::vector<int> lookup(const std::vector<RuleTerm> &pattern, std::size_t offset = 0) const {
                if (offset >= pattern.size()) {
                    return value.value_or(std::vector<int>{});
                }

                const auto &term = pattern[offset];
                const auto &children = term.isWildcard ? wildcards : exactChildren;
                const auto it = children.find(term.key);
                if (it == children.end()) {
                    return {};
                }
                return it->second->lookup(pattern, offset + 1);
            }

            std::vector<RuleTerm> findBestPath(const std::vector<QueryTerm> &query) const {
                auto paths = findPaths(query, 0);
                if (paths.empty()) {
                    return {};
                }

                std::vector<RuleTerm> best;
                auto bestLen = -1;
                auto bestExact = -1;
                auto bestTypedWildcard = -1;
                auto bestFirstExact = std::numeric_limits<int>::max();

                for (const auto &path : paths) {
                    const auto len = static_cast<int>(path.size());
                    auto exact = 0;
                    auto typedWildcard = 0;
                    auto firstExact = std::numeric_limits<int>::max();

                    for (auto i = 0; i < static_cast<int>(path.size()); ++i) {
                        const auto &term = path[static_cast<std::size_t>(i)];
                        if (!term.isWildcard) {
                            ++exact;
                            firstExact = std::min(firstExact, i);
                        } else if (term.key != AnyPhonemeType) {
                            ++typedWildcard;
                        }
                    }

                    const auto isBetter = len > bestLen ||
                                          (len == bestLen && exact > bestExact) ||
                                          (len == bestLen && exact == bestExact && typedWildcard > bestTypedWildcard) ||
                                          (len == bestLen && exact == bestExact && typedWildcard == bestTypedWildcard && firstExact < bestFirstExact);
                    if (isBetter) {
                        best = path;
                        bestLen = len;
                        bestExact = exact;
                        bestTypedWildcard = typedWildcard;
                        bestFirstExact = firstExact;
                    }
                }

                return best;
            }

        private:
            std::vector<std::vector<RuleTerm>> findPaths(const std::vector<QueryTerm> &query, std::size_t offset) const {
                if (offset >= query.size()) {
                    return {};
                }

                const auto &term = query[offset];
                std::vector<std::vector<RuleTerm>> paths;

                if (const auto it = exactChildren.find(term.specifiedKey); it != exactChildren.end()) {
                    if (it->second->value.has_value()) {
                        paths.push_back({RuleTerm{term.specifiedKey, false}});
                    }
                    for (auto &subpath : it->second->findPaths(query, offset + 1)) {
                        subpath.insert(subpath.begin(), RuleTerm{term.specifiedKey, false});
                        paths.push_back(std::move(subpath));
                    }
                }

                if (!term.wildcardKey.empty()) {
                    if (const auto it = wildcards.find(term.wildcardKey); it != wildcards.end()) {
                        if (it->second->value.has_value()) {
                            paths.push_back({RuleTerm{term.wildcardKey, true}});
                        }
                        for (auto &subpath : it->second->findPaths(query, offset + 1)) {
                            subpath.insert(subpath.begin(), RuleTerm{term.wildcardKey, true});
                            paths.push_back(std::move(subpath));
                        }
                    }
                }

                if (const auto it = wildcards.find(AnyPhonemeType); it != wildcards.end()) {
                    if (it->second->value.has_value()) {
                        paths.push_back({RuleTerm{AnyPhonemeType, true}});
                    }
                    for (auto &subpath : it->second->findPaths(query, offset + 1)) {
                        subpath.insert(subpath.begin(), RuleTerm{AnyPhonemeType, true});
                        paths.push_back(std::move(subpath));
                    }
                }

                return paths;
            }

            std::unordered_map<std::string, std::unique_ptr<TrieNode>> exactChildren;
            std::unordered_map<std::string, std::unique_ptr<TrieNode>> wildcards;
            std::optional<std::vector<int>> value;
        };

        std::unordered_map<std::string, std::string> phonemeTypes;
        TrieNode trie;
    };

    namespace {

        std::string ruleField(std::size_t index, const std::string &field) {
            return "rules[" + std::to_string(index) + "]." + field;
        }

        std::vector<std::string> parsePattern(const nlohmann::json &rule, std::size_t ruleIndex) {
            const auto field = ruleField(ruleIndex, "pattern");
            if (!rule.contains("pattern") || !rule["pattern"].is_array() || rule["pattern"].empty()) {
                throw parseError(field + " must be a non-empty string array");
            }

            std::vector<std::string> pattern;
            pattern.reserve(rule["pattern"].size());

            for (std::size_t i = 0; i < rule["pattern"].size(); ++i) {
                const auto &term = rule["pattern"][i];
                if (!term.is_string()) {
                    throw parseError(field + "[" + std::to_string(i) + "] must be a string");
                }

                auto value = term.get<std::string>();
                if (value.empty()) {
                    throw parseError(field + "[" + std::to_string(i) + "] must not be empty");
                }
                pattern.push_back(std::move(value));
            }

            return pattern;
        }

        std::vector<int> parseOnsets(const nlohmann::json &rule, std::size_t ruleIndex, std::size_t patternSize) {
            const auto field = ruleField(ruleIndex, "onsets");
            if (!rule.contains("onsets") || !rule["onsets"].is_array()) {
                throw parseError(field + " must be an integer array");
            }

            std::vector<int> onsets;
            onsets.reserve(rule["onsets"].size());

            for (std::size_t i = 0; i < rule["onsets"].size(); ++i) {
                const auto &onset = rule["onsets"][i];
                if (!onset.is_number_integer()) {
                    throw parseError(field + "[" + std::to_string(i) + "] must be an integer");
                }

                auto value = 0;
                if (onset.is_number_unsigned()) {
                    const auto unsignedValue = onset.get<unsigned long long>();
                    if (unsignedValue > static_cast<unsigned long long>(std::numeric_limits<int>::max()) || unsignedValue >= patternSize) {
                        throw parseError(field + "[" + std::to_string(i) + "] is out of range");
                    }
                    value = static_cast<int>(unsignedValue);
                } else {
                    const auto signedValue = onset.get<long long>();
                    if (signedValue < 0 || signedValue > std::numeric_limits<int>::max() || static_cast<std::size_t>(signedValue) >= patternSize) {
                        throw parseError(field + "[" + std::to_string(i) + "] is out of range");
                    }
                    value = static_cast<int>(signedValue);
                }
                onsets.push_back(value);
            }

            return onsets;
        }

    }

    RuleOnsetMarker::RuleOnsetMarker() : d(std::make_unique<Private>()) {
    }

    RuleOnsetMarker::~RuleOnsetMarker() = default;

    RuleOnsetMarker::RuleOnsetMarker(RuleOnsetMarker &&) noexcept = default;

    RuleOnsetMarker &RuleOnsetMarker::operator=(RuleOnsetMarker &&) noexcept = default;

    srt::core::Expected<std::unique_ptr<RuleOnsetMarker>>
    RuleOnsetMarker::create(std::istream &ruleDefinitionFile) {
        auto obj = std::unique_ptr<RuleOnsetMarker>(new RuleOnsetMarker());

        try {
            nlohmann::json root;
            ruleDefinitionFile >> root;

            if (ruleDefinitionFile.bad()) {
                throw parseError("failed to read rule definition stream");
            }

            if (!root.is_object()) {
                throw parseError("root must be an object");
            }

            if (!root.contains("phonemeTypes") || !root["phonemeTypes"].is_object() || root["phonemeTypes"].empty()) {
                throw parseError("phonemeTypes must be a non-empty object");
            }

            for (auto it = root["phonemeTypes"].begin(); it != root["phonemeTypes"].end(); ++it) {
                if (!it.value().is_string()) {
                    throw parseError("phonemeTypes." + it.key() + " must be a string");
                }
                auto value = it.value().get<std::string>();
                if (value.empty()) {
                    throw parseError("phonemeTypes." + it.key() + " must not be empty");
                }
                if (value == AnyPhonemeType) {
                    throw parseError("phonemeTypes." + it.key() + " must not use reserved type " + AnyPhonemeType);
                }
                obj->d->phonemeTypes.emplace(it.key(), std::move(value));
            }

            if (!root.contains("rules") || !root["rules"].is_array()) {
                throw parseError("rules must be an array");
            }

            const auto &rules = root["rules"];
            for (std::size_t ruleIndex = 0; ruleIndex < rules.size(); ++ruleIndex) {
                const auto &rule = rules[ruleIndex];
                if (!rule.is_object()) {
                    throw parseError("rules[" + std::to_string(ruleIndex) + "] must be an object");
                }

                auto patternKeys = parsePattern(rule, ruleIndex);
                auto onsets = parseOnsets(rule, ruleIndex, patternKeys.size());

                std::vector<RuleTerm> pattern;
                pattern.reserve(patternKeys.size());
                for (auto &key : patternKeys) {
                    pattern.push_back(RuleTerm{std::move(key), true});
                }

                obj->d->trie.insert(pattern, onsets);
            }
        } catch (const std::exception &e) {
            return srt::core::Error(srt::core::Error::InvalidFormat, e.what());
        }

        return obj;
    }

    std::vector<bool> RuleOnsetMarker::mark(const std::vector<std::string> &phonemeSequence) const {
        std::vector<QueryTerm> query;
        query.reserve(phonemeSequence.size());

        for (const auto &phoneme : phonemeSequence) {
            auto type = std::string{};
            if (const auto it = d->phonemeTypes.find(phoneme); it != d->phonemeTypes.end()) {
                type = it->second;
            }
            query.push_back(QueryTerm{phoneme, std::move(type)});
        }

        std::vector<bool> isOnset(phonemeSequence.size(), false);

        std::size_t i = 0;
        while (i < query.size()) {
            const auto subQuery = std::vector<QueryTerm>(query.begin() + static_cast<std::ptrdiff_t>(i), query.end());
            const auto bestPath = d->trie.findBestPath(subQuery);

            if (bestPath.empty()) {
                ++i;
                continue;
            }

            const auto onsets = d->trie.lookup(bestPath);
            for (const auto onset : onsets) {
                const auto index = i + static_cast<std::size_t>(onset);
                if (index < isOnset.size()) {
                    isOnset[index] = true;
                }
            }

            i += bestPath.size();
        }

        return isOnset;
    }

}
