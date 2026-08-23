#include "PackageLoader_p.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

#include "ContribCategory.h"
#include "ContribCategory_p.h"
#include "ContribCreateContext_p.h"
#include "ContribReference.h"
#include "ContribSpec_p.h"
#include "PackageHandle_p.h"
#include "SynthUnit_p.h"

namespace fs = std::filesystem;

namespace srt {

    namespace {

        constexpr int supportedRuntimeLevel = 1;
        constexpr std::string_view supportedManifestVersion = "1.0";

        using Variables = std::map<std::string, std::string, std::less<>>;

        class JsonProfileValidator {
        public:
            explicit JsonProfileValidator(std::string_view source) : m_source(source) {
            }

            bool hasDuplicateObjectKey() {
                skipSpace();
                return scanValue() && m_duplicate;
            }

        private:
            void skipSpace() {
                for (;;) {
                    while (m_offset < m_source.size() && stdc::str::is_space(m_source[m_offset])) {
                        ++m_offset;
                    }
                    if (m_offset + 1 >= m_source.size() || m_source[m_offset] != '/') {
                        return;
                    }
                    if (m_source[m_offset + 1] == '/') {
                        m_offset += 2;
                        while (m_offset < m_source.size() && m_source[m_offset] != '\r' &&
                               m_source[m_offset] != '\n') {
                            ++m_offset;
                        }
                        continue;
                    }
                    if (m_source[m_offset + 1] != '*') {
                        return;
                    }
                    const auto end = m_source.find("*/", m_offset + 2);
                    if (end == std::string_view::npos) {
                        m_offset = m_source.size();
                        return;
                    }
                    m_offset = end + 2;
                }
            }

            bool scanString(std::string *decoded = nullptr) {
                if (m_offset >= m_source.size() || m_source[m_offset] != '"') {
                    return false;
                }
                const auto begin = m_offset++;
                bool escaped = false;
                while (m_offset < m_source.size()) {
                    const auto ch = m_source[m_offset++];
                    if (escaped) {
                        escaped = false;
                        continue;
                    }
                    if (ch == '\\') {
                        escaped = true;
                        continue;
                    }
                    if (ch != '"') {
                        continue;
                    }
                    if (decoded) {
                        stdc::json::ParseError error;
                        auto value = JsonValue::fromJson(m_source.substr(begin, m_offset - begin),
                                                         false, &error);
                        if (error || !value.isString()) {
                            return false;
                        }
                        *decoded = value.toString();
                    }
                    return true;
                }
                return false;
            }

            bool scanValue() {
                skipSpace();
                if (m_offset >= m_source.size()) {
                    return false;
                }
                if (m_source[m_offset] == '{') {
                    return scanObject();
                }
                if (m_source[m_offset] == '[') {
                    return scanArray();
                }
                if (m_source[m_offset] == '"') {
                    return scanString();
                }
                while (m_offset < m_source.size()) {
                    const auto ch = m_source[m_offset];
                    if (ch == ',' || ch == '}' || ch == ']' || stdc::str::is_space(ch)) {
                        break;
                    }
                    ++m_offset;
                }
                return true;
            }

            bool scanObject() {
                ++m_offset;
                skipSpace();
                std::set<std::string, std::less<>> keys;
                if (m_offset < m_source.size() && m_source[m_offset] == '}') {
                    ++m_offset;
                    return true;
                }
                for (;;) {
                    std::string key;
                    if (!scanString(&key)) {
                        return false;
                    }
                    if (!keys.insert(std::move(key)).second) {
                        m_duplicate = true;
                    }
                    skipSpace();
                    if (m_offset >= m_source.size() || m_source[m_offset++] != ':') {
                        return false;
                    }
                    if (!scanValue()) {
                        return false;
                    }
                    skipSpace();
                    if (m_offset >= m_source.size()) {
                        return false;
                    }
                    const auto delimiter = m_source[m_offset++];
                    if (delimiter == '}') {
                        return true;
                    }
                    if (delimiter != ',') {
                        return false;
                    }
                    skipSpace();
                }
            }

            bool scanArray() {
                ++m_offset;
                skipSpace();
                if (m_offset < m_source.size() && m_source[m_offset] == ']') {
                    ++m_offset;
                    return true;
                }
                for (;;) {
                    if (!scanValue()) {
                        return false;
                    }
                    skipSpace();
                    if (m_offset >= m_source.size()) {
                        return false;
                    }
                    const auto delimiter = m_source[m_offset++];
                    if (delimiter == ']') {
                        return true;
                    }
                    if (delimiter != ',') {
                        return false;
                    }
                }
            }

            std::string_view m_source;
            std::size_t m_offset = 0;
            bool m_duplicate = false;
        };

        std::string displayPath(const fs::path &path) {
            return stdc::path::to_utf8(path);
        }

        Expected<JsonObject> readJsonObject(const fs::path &path) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return Error(Error::FileNotOpen,
                             displayPath(path) + ": failed to open JSON declaration");
            }

            std::stringstream stream;
            stream << file.rdbuf();
            auto text = stream.str();
            if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
                static_cast<unsigned char>(text[1]) == 0xbb &&
                static_cast<unsigned char>(text[2]) == 0xbf) {
                text.erase(0, 3);
            }

            if (JsonProfileValidator(text).hasDuplicateObjectKey()) {
                return Error(Error::InvalidFormat,
                             displayPath(path) + ": declaration contains a duplicate object key");
            }

            stdc::json::ParseError parseError;
            auto value = JsonValue::fromJson(text, true, &parseError);
            if (parseError) {
                return Error(Error::InvalidFormat, displayPath(path) + ": " + parseError.message());
            }
            if (!value.isObject()) {
                return Error(Error::InvalidFormat,
                             displayPath(path) + ": declaration root must be an object");
            }
            return value.toObject();
        }

        bool isVariableName(std::string_view value) {
            if (value.empty() || (!stdc::str::is_alpha(value.front()) && value.front() != '_')) {
                return false;
            }
            return std::all_of(value.begin() + 1, value.end(),
                               [](char ch) { return stdc::str::is_alnum(ch) || ch == '_'; });
        }

        class TemplateExpander {
        public:
            TemplateExpander(std::string_view source,
                             const std::function<std::string_view(std::string_view)> &lookup)
                : m_source(source), m_lookup(lookup) {
            }

            Expected<std::string> expand() {
                return expandRange(false, 0);
            }

        private:
            Expected<std::string> expandRange(bool nested, std::size_t depth) {
                if (depth > 256) {
                    return Error(Error::InvalidFormat, "string variable nesting is too deep");
                }

                std::string result;
                while (m_offset < m_source.size()) {
                    const auto ch = m_source[m_offset];
                    if (nested && ch == '}') {
                        return result;
                    }
                    if (ch != '$') {
                        result += ch;
                        ++m_offset;
                        continue;
                    }
                    if (m_offset + 1 >= m_source.size()) {
                        result += '$';
                        ++m_offset;
                        continue;
                    }
                    if (m_source[m_offset + 1] == '$') {
                        result += '$';
                        m_offset += 2;
                        continue;
                    }
                    if (m_source[m_offset + 1] != '{') {
                        result += '$';
                        ++m_offset;
                        continue;
                    }

                    m_offset += 2;
                    auto payload = expandRange(true, depth + 1);
                    if (!payload) {
                        return payload.takeError();
                    }
                    if (m_offset >= m_source.size() || m_source[m_offset] != '}') {
                        return Error(Error::InvalidFormat,
                                     "string variable reference has no closing brace");
                    }
                    ++m_offset;

                    const auto &name = payload.get();
                    if (isVariableName(name)) {
                        result += m_lookup(name);
                    }
                }

                if (nested) {
                    return Error(Error::InvalidFormat,
                                 "string variable reference has no closing brace");
                }
                return result;
            }

            std::string_view m_source;
            const std::function<std::string_view(std::string_view)> &m_lookup;
            std::size_t m_offset = 0;
        };

        Expected<std::string> expandString(std::string_view source, const Variables &variables) {
            const auto lookup = [&variables](std::string_view name) -> std::string_view {
                const auto it = variables.find(name);
                return it == variables.end() ? std::string_view() : std::string_view(it->second);
            };
            return TemplateExpander(source, lookup).expand();
        }

        Expected<Variables> readVariables(const JsonObject &object, const Variables &outer) {
            Variables values;
            const auto varsIt = object.find("vars");
            if (varsIt == object.end()) {
                return values;
            }
            if (!varsIt->second.isArray()) {
                return Error(Error::InvalidFormat, "vars must be an array");
            }

            for (const auto &itemValue : varsIt->second.toArray()) {
                if (!itemValue.isObject()) {
                    return Error(Error::InvalidFormat, "each vars item must be an object");
                }
                const auto &item = itemValue.toObject();
                if (item.size() != 2 || item.find("name") == item.end() ||
                    item.find("value") == item.end() || !item.at("name").isString() ||
                    !item.at("value").isString()) {
                    return Error(Error::InvalidFormat,
                                 "each vars item must contain only string name and value fields");
                }

                const auto name = item.at("name").toString();
                if (!isVariableName(name) || name == "root" || name == "dir") {
                    return Error(Error::InvalidFormat, "vars item has an invalid name");
                }
                if (values.find(name) != values.end()) {
                    return Error(Error::InvalidFormat, "vars contains a duplicate name");
                }

                Variables visible = outer;
                for (const auto &item : values) {
                    visible.insert_or_assign(item.first, item.second);
                }
                auto expanded = expandString(item.at("value").toString(), visible);
                if (!expanded) {
                    return expanded.takeError().withContext("failed to expand vars item");
                }
                values.emplace(name, expanded.take());
            }
            return values;
        }

        Expected<void> expandJson(JsonValue &value, const Variables &variables) {
            if (auto *string = value.asString()) {
                auto expanded = expandString(*string, variables);
                if (!expanded) {
                    return expanded.takeError();
                }
                *string = expanded.take();
                return {};
            }
            if (auto *array = value.asArray()) {
                for (auto &item : *array) {
                    auto result = expandJson(item, variables);
                    if (!result) {
                        return result.takeError();
                    }
                }
                return {};
            }
            if (auto *object = value.asObject()) {
                for (auto &item : *object) {
                    auto result = expandJson(item.second, variables);
                    if (!result) {
                        return result.takeError();
                    }
                }
            }
            return {};
        }

        Expected<Variables> expandDeclaration(JsonObject &object, const Variables &outer,
                                              const fs::path &root, const fs::path &directory,
                                              bool preserveVersion) {
            Variables builtins = outer;
            builtins.insert_or_assign("root", displayPath(root));
            builtins.insert_or_assign("dir", displayPath(directory));

            auto localResult = readVariables(object, builtins);
            if (!localResult) {
                return localResult.takeError();
            }
            auto local = localResult.take();
            Variables visible = std::move(builtins);
            for (const auto &item : local) {
                visible.insert_or_assign(item.first, item.second);
            }

            object.erase("vars");
            for (auto &item : object) {
                if (preserveVersion && item.first == "$version") {
                    continue;
                }
                auto result = expandJson(item.second, visible);
                if (!result) {
                    return result.takeError().withContext("failed to expand declaration field");
                }
            }
            return local;
        }

        Expected<stdc::VersionNumber> readVersion(const JsonValue &value, std::string_view field) {
            if (!value.isString()) {
                return Error(Error::InvalidFormat,
                             std::string(field) + " must be a version string");
            }
            const auto &text = value.toString();
            std::size_t count = 0;
            std::size_t begin = 0;
            while (begin < text.size()) {
                const auto end = text.find('.', begin);
                const auto component =
                    text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
                if (component.empty() || ++count > 4 ||
                    (component.size() > 1 && component.front() == '0') ||
                    !std::all_of(component.begin(), component.end(),
                                 [](char ch) { return stdc::str::is_digit(ch); })) {
                    return Error(Error::InvalidFormat,
                                 std::string(field) + " has an invalid version");
                }
                if (end == std::string::npos) {
                    break;
                }
                begin = end + 1;
            }
            if (count == 0) {
                return Error(Error::InvalidFormat, std::string(field) + " has an invalid version");
            }
            const auto version = stdc::VersionNumber::fromString(text);
            if (!version) {
                return Error(Error::InvalidFormat,
                             std::string(field) + " version is not representable");
            }
            return *version;
        }

        fs::path pathFromManifest(std::string_view text) {
            std::string normalized(text);
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            return stdc::path::from_utf8(normalized);
        }

        fs::path resolvePath(const fs::path &base, std::string_view text) {
            auto path = pathFromManifest(text);
            if (path.is_relative()) {
                path = base / path;
            }
            return path.lexically_normal();
        }

        Expected<DisplayText> readDisplayText(const JsonValue &value, std::string_view field,
                                              const fs::path *pathBase = nullptr) {
            const auto convert = [pathBase,
                                  field](const std::string &text) -> Expected<std::string> {
                if (pathBase && text.find('\0') != std::string::npos) {
                    return Error(Error::InvalidFormat,
                                 std::string(field) + " path must not contain NUL");
                }
                return pathBase ? displayPath(resolvePath(*pathBase, text)) : text;
            };

            if (value.isString()) {
                auto converted = convert(value.toString());
                if (!converted) {
                    return converted.takeError();
                }
                return DisplayText(converted.take());
            }
            if (!value.isObject()) {
                return Error(Error::InvalidFormat,
                             std::string(field) + " must be a string or language map");
            }
            const auto &object = value.toObject();
            const auto defaultIt = object.find("_");
            if (defaultIt == object.end() || !defaultIt->second.isString()) {
                return Error(Error::InvalidFormat,
                             std::string(field) + " language map requires a string _ field");
            }

            std::map<std::string, std::string> localized;
            for (const auto &item : object) {
                if (!item.second.isString()) {
                    return Error(Error::InvalidFormat,
                                 std::string(field) + " language map values must be strings");
                }
                if (item.first != "_") {
                    auto converted = convert(item.second.toString());
                    if (!converted) {
                        return converted.takeError();
                    }
                    localized.emplace(item.first, converted.take());
                }
            }
            auto defaultText = convert(defaultIt->second.toString());
            if (!defaultText) {
                return defaultText.takeError();
            }
            return DisplayText(defaultText.take(), localized);
        }

        Expected<void> rejectUnknownFields(const JsonObject &object,
                                           const std::set<std::string_view> &allowed,
                                           std::string_view declaration) {
            for (const auto &item : object) {
                if (allowed.find(item.first) == allowed.end()) {
                    return Error(Error::InvalidFormat,
                                 std::string(declaration) + " has unknown field " + item.first);
                }
            }
            return {};
        }

    }

    PackageLoader::PackageLoader(SynthUnit &synthUnit) : m_synthUnit(&synthUnit) {
    }

    Expected<PackageHandle> PackageLoader::open(const fs::path &path, SynthUnit::OpenMode mode) {
        m_synthUnit->_impl->packageLoadingBegun = true;

        switch (mode) {
            case SynthUnit::DataOnly:
                return openDataOnly(path);
            case SynthUnit::Load:
                return openLoaded(path);
            default:
                return Error(Error::InvalidArgument, "unknown Package open mode");
        }
    }

    Expected<PackageHandle> PackageLoader::openDataOnly(const fs::path &path) {
        auto package = readPackage(path);
        if (!package) {
            return package.takeError();
        }
        return PackageHandle(package.take());
    }

    Expected<PackageHandle> PackageLoader::openLoaded(const fs::path &path) {
        auto rootResult = readPackage(path);
        if (!rootResult) {
            return rootResult.takeError();
        }
        auto root = rootResult.take();
        if (auto loaded = m_synthUnit->findLoadedPackage(root->id, root->version)) {
            return std::move(*loaded);
        }

        using Identity = std::pair<std::string, stdc::VersionNumber>;
        using PackageList = std::vector<std::shared_ptr<PackageData>>;

        std::vector<PackageList> catalog;
        std::set<Identity> discoveredIdentities;
        for (const auto &searchPathValue : m_synthUnit->_impl->packagePaths) {
            std::error_code error;
            auto searchPath = fs::absolute(searchPathValue, error).lexically_normal();
            if (error || !fs::is_directory(searchPath, error) || error) {
                catalog.emplace_back();
                continue;
            }

            std::vector<fs::path> directories;
            fs::directory_iterator iterator(searchPath, error);
            const fs::directory_iterator end;
            while (!error && iterator != end) {
                if (iterator->is_directory(error) && !error) {
                    directories.push_back(iterator->path());
                }
                error.clear();
                iterator.increment(error);
            }
            std::sort(directories.begin(), directories.end(), [](const auto &LHS, const auto &RHS) {
                return displayPath(LHS.filename()) < displayPath(RHS.filename());
            });

            PackageList packages;
            std::set<Identity> pathIdentities;
            for (const auto &directory : directories) {
                auto packageResult = readPackage(directory, true);
                if (!packageResult) {
                    continue;
                }
                auto package = packageResult.take();
                Identity identity(package->id, package->version);
                if (!pathIdentities.insert(identity).second) {
                    return Error(Error::FileDuplicated,
                                 "one Package search path contains a duplicate identity");
                }
                if (discoveredIdentities.insert(identity).second) {
                    packages.push_back(std::move(package));
                }
            }
            catalog.push_back(std::move(packages));
        }

        std::map<Identity, std::shared_ptr<PackageData>> transaction;
        std::map<Identity, int> states;
        std::vector<Identity> stack;
        const Identity rootIdentity(root->id, root->version);
        transaction.emplace(rootIdentity, root);

        const auto selectDependency =
            [&](const PackageDependency &dependency) -> Expected<std::shared_ptr<PackageData>> {
            for (const auto &pathPackages : catalog) {
                std::shared_ptr<PackageData> selected;
                for (const auto &candidate : pathPackages) {
                    if (candidate->id != dependency.id ||
                        candidate->compatVersion > dependency.version ||
                        candidate->version < dependency.version) {
                        continue;
                    }
                    if (!selected || candidate->version > selected->version) {
                        selected = candidate;
                    }
                }
                if (!selected) {
                    continue;
                }

                const Identity identity(selected->id, selected->version);
                if (const auto it = transaction.find(identity); it != transaction.end()) {
                    return it->second;
                }
                if (auto loaded = m_synthUnit->findLoadedPackage(selected->id, selected->version)) {
                    return loaded->m_data;
                }
                auto selectedResult = readPackage(selected->path);
                if (!selectedResult) {
                    return selectedResult.takeError().withContext(
                        "selected Package failed full Probe");
                }
                auto selectedPackage = selectedResult.take();
                transaction.emplace(identity, selectedPackage);
                return selectedPackage;
            }
            return Error(Error::FeatureNotSupported,
                         "no installed Package satisfies dependency " + dependency.id);
        };

        const auto resolveTarget = [](const std::shared_ptr<PackageData> &package,
                                      const ContribReference &reference) -> ContribSpec * {
            PackageData *targetPackage = package.get();
            if (!reference.isLocal()) {
                const auto dependency = package->dependencyBindings.find(reference.packageId());
                if (dependency == package->dependencyBindings.end()) {
                    return nullptr;
                }
                targetPackage = dependency->second.get();
            }
            const auto category = targetPackage->contributionIndex.find(reference.category());
            if (category == targetPackage->contributionIndex.end()) {
                return nullptr;
            }
            const auto contribution = category->second.find(reference.contributionId());
            return contribution == category->second.end() ? nullptr : contribution->second;
        };

        std::function<Expected<void>(const std::shared_ptr<PackageData> &)> probePackage;
        probePackage = [&](const std::shared_ptr<PackageData> &package) -> Expected<void> {
            if (package->loaded) {
                return {};
            }

            const Identity identity(package->id, package->version);
            const auto state = states[identity];
            if (state == 2) {
                return {};
            }
            if (state == 1) {
                std::string chain;
                const auto begin = std::find(stack.begin(), stack.end(), identity);
                for (auto it = begin; it != stack.end(); ++it) {
                    if (!chain.empty()) {
                        chain += " -> ";
                    }
                    chain += it->first + "@" + it->second.toString();
                }
                chain += " -> " + identity.first + "@" + identity.second.toString();
                return Error(Error::RecursiveDependency, "Package dependency cycle: " + chain);
            }

            states[identity] = 1;
            stack.push_back(identity);
            for (const auto &dependency : package->dependencies) {
                auto selected = selectDependency(dependency);
                if (!selected) {
                    return selected.takeError().withContext("failed to resolve dependency of " +
                                                            package->id);
                }
                package->dependencyBindings.emplace(dependency.id, selected.get());
                auto result = probePackage(selected.get());
                if (!result) {
                    return result.takeError();
                }
            }

            for (const auto &categoryEntry : package->contributions) {
                auto *category = m_synthUnit->category(categoryEntry.first);
                if (!category) {
                    return Error(Error::FeatureNotSupported,
                                 "contribution category disappeared during Probe");
                }
                if (category->declarationMode() != ContribCategory::ModuleDeclaration) {
                    continue;
                }
                for (auto *spec : categoryEntry.second) {
                    auto *loader = m_synthUnit->_impl->pluginFactory.findInterpreter(
                        category->interpreterIid(), spec->interface(), spec->variant(),
                        spec->level());
                    if (!loader) {
                        return Error(Error::FeatureNotSupported,
                                     "no interpreter provides the requested module triple");
                    }
                    spec->_impl->pluginLoader = loader;

                    for (const auto &import : spec->_impl->imports) {
                        auto *target = resolveTarget(package, import.reference());
                        if (!target) {
                            return Error(Error::FeatureNotSupported,
                                         "module import target does not exist");
                        }
                        auto *targetCategory =
                            m_synthUnit->category(target->reference().category());
                        if (!targetCategory || targetCategory->declarationMode() !=
                                                   ContribCategory::ModuleDeclaration) {
                            return Error(Error::InvalidFormat,
                                         "module import target is not a module contribution");
                        }
                    }
                }
            }

            stack.pop_back();
            states[identity] = 2;
            return {};
        };

        auto probeResult = probePackage(root);
        if (!probeResult) {
            for (const auto &packageEntry : transaction) {
                packageEntry.second->dependencyBindings.clear();
            }
            return probeResult.takeError();
        }

        for (const auto &packageEntry : transaction) {
            const auto &package = packageEntry.second;
            if (package->loaded) {
                continue;
            }
            for (const auto &categoryEntry : package->contributions) {
                auto *category = m_synthUnit->category(categoryEntry.first);
                if (category->declarationMode() != ContribCategory::ModuleDeclaration) {
                    continue;
                }
                for (auto *spec : categoryEntry.second) {
                    auto interpreterResult = m_synthUnit->_impl->pluginFactory.loadInterpreter(
                        spec->_impl->pluginLoader);
                    if (!interpreterResult) {
                        return interpreterResult.takeError();
                    }
                    spec->_impl->interpreter = interpreterResult.get();

                    auto exports = spec->_impl->interpreter->createExports(*spec);
                    if (!exports) {
                        return exports.takeError().withContext(
                            "failed to interpret module exports");
                    }
                    spec->_impl->exports = exports.take();

                    auto configuration = spec->_impl->interpreter->createConfiguration(*spec);
                    if (!configuration) {
                        return configuration.takeError().withContext(
                            "failed to interpret module configuration");
                    }
                    spec->_impl->configuration = configuration.take();
                }
            }
        }

        for (const auto &packageEntry : transaction) {
            const auto &package = packageEntry.second;
            if (package->loaded) {
                continue;
            }
            for (const auto &categoryEntry : package->contributions) {
                auto *category = m_synthUnit->category(categoryEntry.first);
                if (category->declarationMode() != ContribCategory::ModuleDeclaration) {
                    continue;
                }
                for (auto *spec : categoryEntry.second) {
                    for (auto &import : spec->_impl->imports) {
                        auto *target = resolveTarget(package, import.reference());
                        auto options = target->_impl->interpreter->createImportOptions(
                            *target, import.manifestOptions());
                        if (!options) {
                            return options.takeError().withContext(
                                "failed to interpret module import options");
                        }
                        import._impl->options = options.take();
                    }
                    auto validation = spec->_impl->interpreter->validateImports(*spec);
                    if (!validation) {
                        return validation.takeError().withContext(
                            "module imports failed interpreter validation");
                    }
                }
            }
        }

        for (const auto &packageEntry : transaction) {
            const auto &package = packageEntry.second;
            if (package->loaded) {
                continue;
            }
            m_synthUnit->_impl->packages[package->id].insert_or_assign(package->version, package);
            for (const auto &categoryEntry : package->contributions) {
                auto *category = m_synthUnit->category(categoryEntry.first);
                category->_impl->contributions.insert(category->_impl->contributions.end(),
                                                      categoryEntry.second.begin(),
                                                      categoryEntry.second.end());
            }
        }
        for (const auto &packageEntry : transaction) {
            packageEntry.second->loaded = true;
        }
        return PackageHandle(std::move(root));
    }

    Expected<std::shared_ptr<PackageData>> PackageLoader::readPackage(const fs::path &path,
                                                                      bool candidateOnly) {
        std::error_code pathError;
        auto root = fs::absolute(path, pathError).lexically_normal();
        if (pathError || !fs::is_directory(root, pathError) || pathError) {
            return Error(Error::FileNotFound, "Package path is not a readable directory");
        }

        const auto descPath = root / "desc.json";
        if (!fs::is_regular_file(descPath, pathError) || pathError) {
            return Error(Error::FileNotFound, "Package root does not contain desc.json");
        }

        auto descResult = readJsonObject(descPath);
        if (!descResult) {
            return descResult.takeError();
        }
        auto desc = descResult.take();

        const auto formatIt = desc.find("$version");
        if (formatIt == desc.end() || !formatIt->second.isString() ||
            formatIt->second.toString() != supportedManifestVersion) {
            return Error(Error::FeatureNotSupported,
                         "Package manifest version is missing or unsupported");
        }
        const auto runtimeIt = desc.find("runtimeLevel");
        if (runtimeIt == desc.end() || !runtimeIt->second.isInt() ||
            runtimeIt->second.toInt() <= 0) {
            return Error(Error::InvalidFormat, "runtimeLevel must be a positive integer");
        }
        if (runtimeIt->second.toInt() > supportedRuntimeLevel) {
            return Error(Error::FeatureNotSupported, "Package requires a newer Runtime Level");
        }

        auto variablesResult = expandDeclaration(desc, {}, root, root, true);
        if (!variablesResult) {
            return variablesResult.takeError().withContext("failed to expand desc.json");
        }
        const auto packageVariables = variablesResult.take();

        static const std::set<std::string_view> packageFields = {
            "$version", "compatVersion", "contributes",  "copyright", "dependencies", "description",
            "id",       "readme",        "runtimeLevel", "url",       "vendor",       "version",
        };
        if (auto result = rejectUnknownFields(desc, packageFields, "desc.json"); !result) {
            return result.takeError();
        }

        const auto idIt = desc.find("id");
        if (idIt == desc.end() || !idIt->second.isString() ||
            !ContribReference::isValidPackageId(idIt->second.toString())) {
            return Error(Error::InvalidFormat, "Package id is missing or invalid");
        }

        const auto versionIt = desc.find("version");
        if (versionIt == desc.end()) {
            return Error(Error::InvalidFormat, "Package version is missing");
        }
        auto versionResult = readVersion(versionIt->second, "version");
        if (!versionResult) {
            return versionResult.takeError();
        }
        const auto version = versionResult.take();

        auto compatVersion = version;
        if (const auto it = desc.find("compatVersion"); it != desc.end()) {
            auto compatResult = readVersion(it->second, "compatVersion");
            if (!compatResult) {
                return compatResult.takeError();
            }
            compatVersion = compatResult.take();
            if (compatVersion > version) {
                return Error(Error::InvalidFormat, "compatVersion must not exceed version");
            }
        }

        auto package = std::make_shared<PackageData>(m_synthUnit);
        package->id = idIt->second.toString();
        package->version = version;
        package->compatVersion = compatVersion;
        package->runtimeLevel = static_cast<int>(runtimeIt->second.toInt());
        package->path = root;
        package->name = DisplayText(package->id);

        const auto readOptionalText = [&desc](std::string_view name, DisplayText *destination,
                                              const fs::path *base = nullptr) -> Expected<void> {
            const auto it = desc.find(name);
            if (it == desc.end()) {
                return {};
            }
            auto result = readDisplayText(it->second, name, base);
            if (!result) {
                return result.takeError();
            }
            *destination = result.take();
            return {};
        };

        if (auto result = readOptionalText("vendor", &package->vendor); !result) {
            return result.takeError();
        }
        if (auto result = readOptionalText("copyright", &package->copyright); !result) {
            return result.takeError();
        }
        if (auto result = readOptionalText("description", &package->description); !result) {
            return result.takeError();
        }
        if (auto result = readOptionalText("readme", &package->readme, &root); !result) {
            return result.takeError();
        }
        if (const auto it = desc.find("url"); it != desc.end()) {
            if (!it->second.isString()) {
                return Error(Error::InvalidFormat, "url must be a string");
            }
            package->url = it->second.toString();
        }

        if (const auto dependenciesIt = desc.find("dependencies"); dependenciesIt != desc.end()) {
            if (!dependenciesIt->second.isArray()) {
                return Error(Error::InvalidFormat, "dependencies must be an array");
            }
            std::set<std::string, std::less<>> ids;
            for (const auto &value : dependenciesIt->second.toArray()) {
                auto dependency = PackageDependency::fromJsonValue(value);
                if (!dependency) {
                    return dependency.takeError().withContext("invalid Package dependency");
                }
                if (dependency->id == package->id) {
                    return Error(Error::InvalidFormat, "Package must not depend on itself");
                }
                if (!ids.insert(dependency->id).second) {
                    return Error(Error::InvalidFormat, "dependencies contains a duplicate id");
                }
                package->dependencies.push_back(dependency.take());
            }
        }

        const auto contributesIt = desc.find("contributes");
        if (contributesIt == desc.end()) {
            return package;
        }
        if (!contributesIt->second.isObject()) {
            return Error(Error::InvalidFormat, "contributes must be an object");
        }

        for (const auto &categoryEntry : contributesIt->second.toObject()) {
            if (!ContribReference::isValidDottedId(categoryEntry.first)) {
                return Error(Error::InvalidFormat, "contributes contains an invalid category");
            }
            if (!m_synthUnit->category(categoryEntry.first)) {
                return Error(Error::FeatureNotSupported,
                             "contribution category is not registered: " + categoryEntry.first);
            }
            if (!categoryEntry.second.isArray()) {
                return Error(Error::InvalidFormat, "contribution category value must be an array");
            }
        }
        if (candidateOnly) {
            return package;
        }

        for (const auto &categoryEntry : contributesIt->second.toObject()) {
            const auto &categoryName = categoryEntry.first;
            auto *category = m_synthUnit->category(categoryName);

            std::set<std::string, std::less<>> contributionIds;
            for (const auto &entryValue : categoryEntry.second.toArray()) {
                if (!entryValue.isObject()) {
                    return Error(Error::InvalidFormat, "contribution entry must be an object");
                }

                ContribCreateContext::Data context;
                context.package = package.get();
                context.manifestEntry = entryValue.toObject();
                const auto contributionIdIt = context.manifestEntry.find("id");
                if (contributionIdIt == context.manifestEntry.end() ||
                    !contributionIdIt->second.isString() ||
                    !ContribReference::isValidSegment(contributionIdIt->second.toString())) {
                    return Error(Error::InvalidFormat,
                                 "contribution entry has a missing or invalid id");
                }
                const auto contributionId = contributionIdIt->second.toString();
                if (!contributionIds.insert(contributionId).second) {
                    return Error(Error::InvalidFormat,
                                 "contribution category contains a duplicate id");
                }
                context.reference = ContribReference(categoryName, contributionId);

                if (category->declarationMode() == ContribCategory::ModuleDeclaration) {
                    const auto declarationIt = context.manifestEntry.find("path");
                    if (declarationIt == context.manifestEntry.end() ||
                        !declarationIt->second.isString()) {
                        return Error(Error::InvalidFormat,
                                     "module contribution requires a string path field");
                    }
                    if (declarationIt->second.toString().find('\0') != std::string::npos) {
                        return Error(Error::InvalidFormat,
                                     "module contribution path must not contain NUL");
                    }
                    const auto declarationPath =
                        resolvePath(root, declarationIt->second.toString());
                    auto declarationResult = readJsonObject(declarationPath);
                    if (!declarationResult) {
                        return declarationResult.takeError();
                    }
                    auto declaration = declarationResult.take();
                    if (declaration.find("$version") != declaration.end()) {
                        return Error(Error::InvalidFormat,
                                     "module declaration must not contain $version");
                    }
                    auto moduleVars = expandDeclaration(declaration, packageVariables, root,
                                                        declarationPath.parent_path(), false);
                    if (!moduleVars) {
                        return moduleVars.takeError().withContext(
                            "failed to expand module declaration");
                    }

                    const auto interfaceIt = declaration.find("interface");
                    const auto variantIt = declaration.find("variant");
                    const auto levelIt = declaration.find("level");
                    if (interfaceIt == declaration.end() || !interfaceIt->second.isString() ||
                        !ContribReference::isValidDottedId(interfaceIt->second.toString())) {
                        return Error(Error::InvalidFormat,
                                     "module interface is missing or invalid");
                    }
                    if (variantIt == declaration.end() || !variantIt->second.isString() ||
                        !ContribReference::isValidDottedId(variantIt->second.toString())) {
                        return Error(Error::InvalidFormat, "module variant is missing or invalid");
                    }
                    if (levelIt == declaration.end() || !levelIt->second.isInt() ||
                        levelIt->second.toInt() <= 0 ||
                        levelIt->second.toInt() > std::numeric_limits<int>::max()) {
                        return Error(Error::InvalidFormat, "module level is missing or invalid");
                    }

                    context.declarationPath = declarationPath;
                    context.manifestDeclaration = declaration;
                    context.interface = interfaceIt->second.toString();
                    context.variant = variantIt->second.toString();
                    context.level = static_cast<int>(levelIt->second.toInt());
                    if (const auto it = declaration.find("name"); it != declaration.end()) {
                        auto name = readDisplayText(it->second, "module name");
                        if (!name) {
                            return name.takeError();
                        }
                        context.name = name.take();
                    } else {
                        context.name = DisplayText(contributionId);
                    }
                    if (const auto it = declaration.find("exports"); it != declaration.end()) {
                        context.manifestExports = it->second;
                    }
                    if (const auto it = declaration.find("configuration");
                        it != declaration.end()) {
                        context.manifestConfiguration = it->second;
                    }
                    if (const auto importsIt = declaration.find("imports");
                        importsIt != declaration.end()) {
                        if (!importsIt->second.isArray()) {
                            return Error(Error::InvalidFormat, "module imports must be an array");
                        }
                        for (const auto &importValue : importsIt->second.toArray()) {
                            if (!importValue.isObject()) {
                                return Error(Error::InvalidFormat,
                                             "module import must be an object");
                            }
                            const auto &importObject = importValue.toObject();
                            static const std::set<std::string_view> importFields = {"options",
                                                                                    "ref"};
                            if (auto result = rejectUnknownFields(importObject, importFields,
                                                                  "module import");
                                !result) {
                                return result.takeError();
                            }
                            const auto refIt = importObject.find("ref");
                            if (refIt == importObject.end() || !refIt->second.isString()) {
                                return Error(Error::InvalidFormat,
                                             "module import requires a string ref field");
                            }
                            auto reference = ContribReference::fromString(refIt->second.toString());
                            if (!reference.isValid()) {
                                return Error(Error::InvalidFormat,
                                             "module import has an invalid ref field");
                            }
                            if (!reference.isLocal() &&
                                std::none_of(package->dependencies.begin(),
                                             package->dependencies.end(),
                                             [&](const auto &dependency) {
                                                 return dependency.id == reference.packageId();
                                             })) {
                                return Error(Error::InvalidFormat,
                                             "module import references an undeclared dependency");
                            }
                            JsonValue options;
                            if (const auto it = importObject.find("options");
                                it != importObject.end()) {
                                options = it->second;
                            }
                            context.imports.push_back(
                                ContribSpec::Import(std::move(reference), std::move(options)));
                        }
                    }
                }

                ContribCreateContext createContext(context);
                auto specResult = category->createSpec(createContext);
                if (!specResult) {
                    return specResult.takeError().withContext(
                        "category failed to parse contribution");
                }
                auto spec = specResult.take();
                if (!spec) {
                    return Error(Error::InvalidFormat, "category returned a null contribution");
                }
                auto *specPointer = spec.get();
                package->contributions[categoryName].push_back(specPointer);
                package->contributionIndex[categoryName].emplace(contributionId, specPointer);
                package->ownedContributions.push_back(std::move(spec));
            }
        }

        return package;
    }

}
