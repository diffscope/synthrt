#include "PackageLoader_p.h"

#include <algorithm>
#include <cstddef>
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

#include <stdcorelib/adt/vlarray.h>
#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

#include "ContribCategory.h"
#include "ContribCategory_p.h"
#include "ContribLocator.h"
#include "ContribSpec_p.h"
#include "PackageHandle_p.h"
#include "SynthUnit_p.h"

namespace fs = std::filesystem;

namespace srt {

    namespace {

        constexpr int supportedRuntimeLevel = 1;
        const stdc::VersionNumber supportedManifestVersion(1, 0);

        using Variables = std::map<std::string, std::string, std::less<>>;

        bool isValidImportRole(std::string_view role) {
            if (role.empty()) {
                return false;
            }
            std::size_t begin = 0;
            while (true) {
                const auto end = role.find('/', begin);
                const auto segment = end == std::string_view::npos
                                         ? role.substr(begin)
                                         : role.substr(begin, end - begin);
                if (!ContribLocator::isValidSegment(segment)) {
                    return false;
                }
                if (end == std::string_view::npos) {
                    return true;
                }
                begin = end + 1;
            }
        }

        Expected<void> validatePayloadIdentity(const ContribSpecPayload *payload,
                                               const ContribSpec &spec,
                                               std::string_view payloadName) {
            if (!payload) {
                return Error(Error::InvalidFormat,
                             std::string("interpreter returned null ") + std::string(payloadName));
            }
            if (payload->interface() != spec.interface() || payload->variant() != spec.variant() ||
                payload->level() != spec.level()) {
                return Error(Error::InvalidFormat,
                             stdc::formatN(
                                 "interpreter returned %1 for contract %2/%3/%4 instead of "
                                 "%5/%6/%7",
                                 payloadName, payload->interface(), payload->variant(),
                                 payload->level(), spec.interface(), spec.variant(), spec.level()));
            }
            return {};
        }

        // The JSON parser retains only one value for a repeated key. Scan the source first so the
        // manifest profile can reject duplicates before that information is lost.
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

        Expected<std::string> expandString(std::string_view source, const Variables &variables) {
            const auto lookup = [&variables](std::string_view name) -> std::string {
                if (!isVariableName(name)) {
                    return {};
                }
                const auto it = variables.find(name);
                if (it == variables.end()) {
                    return {};
                }

                // varexp removes one layer of dollar escaping after substitution. Protect dollars
                // from variable values so only the original source participates in tokenization.
                std::string escaped;
                escaped.reserve(it->second.size());
                for (const auto ch : it->second) {
                    if (ch == '$') {
                        escaped += '$';
                    }
                    escaped += ch;
                }
                return escaped;
            };

            return stdc::str::varexp(source, lookup);
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

                // Only earlier entries are visible. This makes forward references empty and
                // prevents a variable cycle without a separate dependency graph.
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
            if (auto string = value.asString()) {
                auto expanded = expandString(*string, variables);
                if (!expanded) {
                    return expanded.takeError();
                }
                *string = expanded.take();
                return {};
            }
            if (auto array = value.asArray()) {
                for (auto &item : *array) {
                    auto result = expandJson(item, variables);
                    if (!result) {
                        return result.takeError();
                    }
                }
                return {};
            }
            if (auto object = value.asObject()) {
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

            // Later parsers receive only usable strings. They must not repeat template expansion.
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

        Expected<PackageDependency> readDependency(const JsonValue &value) {
            if (!value.isObject()) {
                return Error(Error::InvalidFormat, "dependency must be an object");
            }

            const auto &object = value.toObject();
            static const std::set<std::string_view> allowedKeys = {"id", "version"};
            for (const auto &item : object) {
                if (!allowedKeys.count(item.first)) {
                    return Error(Error::InvalidFormat,
                                 std::string("unknown dependency field \"") + item.first + '"');
                }
            }

            const auto id = object.find("id");
            if (id == object.end() || !id->second.isString() ||
                !ContribLocator::isValidPackageId(id->second.toString())) {
                return Error(Error::InvalidFormat, "dependency has a missing or invalid id field");
            }

            const auto version = object.find("version");
            if (version == object.end()) {
                return Error(Error::InvalidFormat,
                             "dependency has a missing or invalid version field");
            }
            auto parsedVersion = readVersion(version->second, "dependency version");
            if (!parsedVersion) {
                return parsedVersion.takeError();
            }

            return PackageDependency(id->second.toString(), parsedVersion.take());
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
        // DataOnly deliberately stops after typed manifest construction. It never performs plugin
        // discovery and never publishes the Package to SynthUnit.
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
        using PackageList = stdc::vlarray<std::shared_ptr<PackageData>>;

        enum class ProbeState {
            Unvisited,
            Visiting,
            Complete,
        };

        // Catalog entries contain only the fields that determine candidacy. Module declarations
        // are read after selection so a broken selected Package fails instead of causing fallback.
        stdc::vlarray<PackageList> catalog;
        std::set<Identity> discoveredIdentities;
        for (const auto &searchPathValue : m_synthUnit->_impl->packagePaths) {
            std::error_code error;
            auto searchPath = fs::absolute(searchPathValue, error).lexically_normal();
            if (error || !fs::is_directory(searchPath, error) || error) {
                catalog.emplace_back();
                continue;
            }

            stdc::vlarray<fs::path> directories;
            fs::directory_iterator iterator(searchPath, error);
            const fs::directory_iterator end;
            while (!error && iterator != end) {
                if (iterator->is_directory(error) && !error) {
                    directories.push_back(iterator->path());
                }
                error.clear();
                iterator.increment(error);
            }
            // Filesystem enumeration order is unspecified. Sorting makes duplicate diagnostics and
            // source discovery reproducible within one search path.
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
                // The first search path containing an identity owns that identity. Later copies
                // are shadowed even when a different dependency edge encounters them first.
                if (discoveredIdentities.insert(identity).second) {
                    packages.push_back(std::move(package));
                }
            }
            catalog.push_back(std::move(packages));
        }

        std::map<Identity, std::shared_ptr<PackageData>> transaction;
        std::map<Identity, ProbeState> states;
        stdc::vlarray<Identity> stack;
        const Identity rootIdentity(root->id, root->version);
        transaction.emplace(rootIdentity, root);

        const auto selectDependency =
            [&](const PackageDependency &dependency) -> Expected<std::shared_ptr<PackageData>> {
            // Search path priority dominates version. Only candidates in the first matching path
            // compete by version.
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
                // Selection is final before the full Probe. A later error must not retry a lower
                // version or another search path.
                auto selectedResult = readPackage(selected->path);
                if (!selectedResult) {
                    return selectedResult.takeError().withContext(
                        "selected Package failed full Probe");
                }
                auto selectedPackage = selectedResult.take();
                transaction.emplace(identity, selectedPackage);
                return selectedPackage;
            }
            return Error(Error::FileNotFound,
                         "no installed Package satisfies dependency " + dependency.id);
        };

        const auto resolveTarget = [](const std::shared_ptr<PackageData> &package,
                                      const ContribLocator &locator) -> ContribSpec * {
            PackageData *targetPackage = package.get();
            if (!locator.isLocal()) {
                const auto dependency = package->dependencyBindings.find(locator.packageId());
                if (dependency == package->dependencyBindings.end()) {
                    return nullptr;
                }
                targetPackage = dependency->second.get();
            }
            const auto category = targetPackage->contributionIndex.find(locator.category());
            if (category == targetPackage->contributionIndex.end()) {
                return nullptr;
            }
            const auto contribution = category->second.find(locator.contributionId());
            return contribution == category->second.end() ? nullptr : contribution->second;
        };

        // WARNING:
        // Probe aborts the transaction on its first error and never resumes traversal. The state
        // map and active stack therefore need no failure unwinding because both are local to this
        // openLoaded() call and are discarded with the failed transaction.
        std::function<Expected<void>(const std::shared_ptr<PackageData> &)> probePackage;
        probePackage = [&](const std::shared_ptr<PackageData> &package) -> Expected<void> {
            if (package->loaded) {
                return {};
            }

            // The three states implement depth first traversal and retain the active stack for a
            // complete dependency cycle diagnostic.
            const Identity identity(package->id, package->version);
            const auto state = states[identity];
            if (state == ProbeState::Complete) {
                return {};
            }
            if (state == ProbeState::Visiting) {
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

            states[identity] = ProbeState::Visiting;
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
                auto category = m_synthUnit->category(categoryEntry.first);
                if (!category) {
                    return Error(Error::FeatureNotSupported,
                                 "contribution category disappeared during Probe");
                }
                if (category->declarationMode() != ContribCategory::ModuleDeclaration) {
                    continue;
                }
                for (auto spec : categoryEntry.second) {
                    auto loader = m_synthUnit->_impl->pluginFactory.findInterpreter(
                        category->interpreterIid(), spec->interface(), spec->level(),
                        spec->variant());
                    if (!loader) {
                        return Error(Error::FeatureNotSupported,
                                     "no interpreter provides the requested module triple");
                    }
                    // Probe records the chosen loader without loading it. This binding remains
                    // fixed for the lifetime of the contribution.
                    spec->_impl->pluginLoader = loader;

                    for (const auto &import : spec->_impl->imports) {
                        auto target = resolveTarget(package, import.locator());
                        if (!target) {
                            return Error(Error::FileNotFound,
                                         "module import target does not exist");
                        }
                        auto targetCategory = m_synthUnit->category(target->locator().category());
                        if (!targetCategory || targetCategory->declarationMode() !=
                                                   ContribCategory::ModuleDeclaration) {
                            return Error(Error::InvalidFormat,
                                         "module import target is not a module contribution");
                        }
                    }
                }
            }

            stack.pop_back();
            states[identity] = ProbeState::Complete;
            return {};
        };

        // Probe performs all dependency and locator work that requires no provider execution.
        auto probeResult = probePackage(root);
        if (!probeResult) {
            // A partial graph can already contain strong edges when cycle detection fails. Remove
            // every temporary edge so transaction objects are released normally.
            for (const auto &packageEntry : transaction) {
                packageEntry.second->dependencyBindings.clear();
            }
            return probeResult.takeError();
        }

        // Acquire loads each selected plugin once and asks it to interpret declarations. Nothing
        // is visible through SynthUnit during this phase.
        for (const auto &packageEntry : transaction) {
            const auto &package = packageEntry.second;
            if (package->loaded) {
                continue;
            }
            for (const auto &categoryEntry : package->contributions) {
                auto category = m_synthUnit->category(categoryEntry.first);
                if (category->declarationMode() != ContribCategory::ModuleDeclaration) {
                    continue;
                }
                for (auto spec : categoryEntry.second) {
                    auto interpreterResult = m_synthUnit->_impl->pluginFactory.loadInterpreter(
                        spec->_impl->pluginLoader, spec->interface(), spec->level(),
                        spec->variant());
                    if (!interpreterResult) {
                        return interpreterResult.takeError();
                    }
                    spec->_impl->interpreter = interpreterResult.get();

                    auto exports = spec->_impl->interpreter->createExports(*spec);
                    if (!exports) {
                        return exports.takeError().withContext(
                            "failed to interpret module exports");
                    }
                    auto typedExports = exports.take();
                    if (auto result = validatePayloadIdentity(typedExports.get(), *spec, "exports");
                        !result) {
                        return result.takeError();
                    }
                    spec->_impl->exports = std::move(typedExports);

                    auto configuration = spec->_impl->interpreter->createConfiguration(*spec);
                    if (!configuration) {
                        return configuration.takeError().withContext(
                            "failed to interpret module configuration");
                    }
                    auto typedConfiguration = configuration.take();
                    if (auto result = validatePayloadIdentity(typedConfiguration.get(), *spec,
                                                              "configuration");
                        !result) {
                        return result.takeError();
                    }
                    spec->_impl->configuration = std::move(typedConfiguration);
                }
            }
        }

        const auto prepareImportBindings = [&]() -> Expected<void> {
            for (const auto &packageEntry : transaction) {
                const auto &package = packageEntry.second;
                if (package->loaded) {
                    continue;
                }
                for (const auto &categoryEntry : package->contributions) {
                    auto category = m_synthUnit->category(categoryEntry.first);
                    if (category->declarationMode() != ContribCategory::ModuleDeclaration) {
                        continue;
                    }
                    for (auto spec : categoryEntry.second) {
                        for (auto &import : spec->_impl->imports) {
                            auto target = resolveTarget(package, import.locator());
                            auto options = target->_impl->interpreter->createImportOptions(
                                *target, import.manifestOptions());
                            if (!options) {
                                return options.takeError().withContext(
                                    "failed to interpret module import options");
                            }
                            auto typedOptions = options.take();
                            if (!typedOptions) {
                                return Error(Error::InvalidFormat,
                                             "target interpreter returned null import options");
                            }
                            if (auto result = validatePayloadIdentity(typedOptions.get(), *target,
                                                                      "import options");
                                !result) {
                                return result.takeError();
                            }
                            spec->_impl->importData.at(import.role()).options =
                                std::move(typedOptions);
                        }
                        for (auto &import : spec->_impl->imports) {
                            auto target = resolveTarget(package, import.locator());
                            auto &importData = spec->_impl->importData.at(import.role());
                            auto binding = spec->_impl->interpreter->createImportBinding(
                                *spec, import, *target, std::move(importData.options));
                            if (!binding) {
                                return binding.takeError().withContext(
                                    "failed to create module import binding");
                            }
                            auto preparedBinding = binding.take();
                            if (!preparedBinding) {
                                return Error(Error::InvalidFormat,
                                             "importer interpreter returned a null import binding");
                            }
                            auto targetCategory =
                                m_synthUnit->category(target->locator().category());
                            auto factory = targetCategory->createExecutiveFactory(*preparedBinding);
                            if (!factory) {
                                return factory.takeError().withContext(
                                    "target category failed to create import execution factory");
                            }
                            importData.binding = std::move(preparedBinding);
                            importData.executiveFactory = factory.take();
                        }
                    }
                }
            }
            return {};
        };

        const auto validatePreparedImports = [&]() -> Expected<void> {
            for (const auto &packageEntry : transaction) {
                const auto &package = packageEntry.second;
                if (package->loaded) {
                    continue;
                }
                for (const auto &categoryEntry : package->contributions) {
                    for (auto spec : categoryEntry.second) {
                        for (auto validator :
                             m_synthUnit->_impl->pluginFactory.importValidators()) {
                            auto validation = validator->validateImports(*spec);
                            if (!validation) {
                                return validation.takeError().withContext(
                                    "prepared contribution imports failed validation");
                            }
                        }
                    }
                }
            }
            return {};
        };

        const auto attachSpecExtensions = [&]() -> Expected<void> {
            for (const auto &packageEntry : transaction) {
                const auto &package = packageEntry.second;
                if (package->loaded) {
                    continue;
                }
                for (const auto &categoryEntry : package->contributions) {
                    for (auto spec : categoryEntry.second) {
                        for (auto interpreter : m_synthUnit->_impl->pluginFactory.interpreters()) {
                            auto extensions = interpreter->createExtensions(*spec);
                            if (!extensions) {
                                return extensions.takeError().withContext(
                                    "contribution extension creation failed");
                            }
                            auto preparedExtensions = extensions.take();
                            for (auto &preparedExtension : preparedExtensions) {
                                if (!preparedExtension) {
                                    return Error(
                                        Error::InvalidFormat,
                                        "contribution interpreter returned null extension");
                                }
                                if (&preparedExtension->spec() != spec) {
                                    return Error(
                                        Error::InvalidFormat,
                                        "contribution extension targets another contribution");
                                }
                                if (!ContribLocator::isValidDottedId(preparedExtension->id())) {
                                    return Error(
                                        Error::InvalidFormat,
                                        "contribution extension has an invalid identifier");
                                }
                                auto id = preparedExtension->id();
                                auto extensionPointer = preparedExtension.get();
                                if (!spec->_impl->extensionData
                                         .emplace(std::move(id), std::move(preparedExtension))
                                         .second) {
                                    return Error(Error::InvalidFormat,
                                                 "contribution extension identifier is duplicated");
                                }
                                spec->_impl->extensions.push_back(extensionPointer);
                            }
                        }
                    }
                }
            }
            return {};
        };

        // Ready pass 1 prepares every import before validators or extensions inspect the graph.
        if (auto result = prepareImportBindings(); !result) {
            return result.takeError();
        }

        // Ready pass 2 validates the complete prepared import graph.
        if (auto result = validatePreparedImports(); !result) {
            return result.takeError();
        }

        // Ready pass 3 attaches extensions without exposing the uncommitted Packages.
        if (auto result = attachSpecExtensions(); !result) {
            return result.takeError();
        }

        // Commit contains no fallible work. The SynthUnit transaction lock prevents readers from
        // observing the registry while the new objects are being inserted.
        for (const auto &packageEntry : transaction) {
            const auto &package = packageEntry.second;
            if (package->loaded) {
                continue;
            }
            m_synthUnit->_impl->packages[package->id].insert_or_assign(package->version, package);
            for (const auto &categoryEntry : package->contributions) {
                auto category = m_synthUnit->category(categoryEntry.first);
                category->_impl->contributions.insert(category->_impl->contributions.end(),
                                                      categoryEntry.second.begin(),
                                                      categoryEntry.second.end());
            }
        }
        // Binding activation cannot fail and occurs under the same visibility barrier as registry
        // publication.
        for (const auto &packageEntry : transaction) {
            const auto &package = packageEntry.second;
            if (package->loaded) {
                continue;
            }
            for (const auto &categoryEntry : package->contributions) {
                for (auto spec : categoryEntry.second) {
                    for (auto &import : spec->_impl->imports) {
                        if (import.binding()) {
                            import.binding()->activateForCommit();
                        }
                    }
                }
            }
        }
        // Mark every Package loaded only after the entire closure has been published.
        for (const auto &packageEntry : transaction) {
            packageEntry.second->loaded = true;
        }
        return PackageHandle(std::move(root));
    }

    Expected<std::shared_ptr<PackageData>> PackageLoader::readPackage(const fs::path &path,
                                                                      bool candidateOnly) {
        std::error_code pathError;
        auto root = fs::absolute(path, pathError).lexically_normal();
        if (pathError) {
            return Error(Error::FileNotOpen, "failed to resolve Package path");
        }
        const auto isDirectory = fs::is_directory(root, pathError);
        if (pathError) {
            return Error(Error::FileNotOpen, "failed to access Package path");
        }
        if (!isDirectory) {
            return Error(Error::FileNotFound, "Package path is not a directory");
        }

        const auto descPath = root / "desc.json";
        const auto isDeclaration = fs::is_regular_file(descPath, pathError);
        if (pathError) {
            return Error(Error::FileNotOpen, "failed to access Package desc.json");
        }
        if (!isDeclaration) {
            return Error(Error::FileNotFound, "Package root does not contain desc.json");
        }

        auto descResult = readJsonObject(descPath);
        if (!descResult) {
            return descResult.takeError();
        }
        auto desc = descResult.take();

        // These gates are intentionally checked before expansion and contribution parsing. An
        // unsupported runtime must not attempt to understand newer declaration structures.
        const auto formatIt = desc.find("$version");
        if (formatIt == desc.end()) {
            return Error(Error::InvalidFormat, "Package manifest version is missing");
        }
        auto manifestVersion = readVersion(formatIt->second, "$version");
        if (!manifestVersion) {
            return manifestVersion.takeError();
        }
        if (manifestVersion.get() != supportedManifestVersion) {
            return Error(Error::FeatureNotSupported, "Package manifest version is unsupported");
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

        const auto idIt = desc.find("id");
        if (idIt == desc.end() || !idIt->second.isString() ||
            !ContribLocator::isValidPackageId(idIt->second.toString())) {
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
                auto dependency = readDependency(value);
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

        const auto contributionsIt = desc.find("contributions");
        if (contributionsIt == desc.end()) {
            package->manifestDeclaration = std::move(desc);
            return package;
        }
        if (!contributionsIt->second.isObject()) {
            return Error(Error::InvalidFormat, "contributions must be an object");
        }

        // Candidate discovery validates category availability without opening module declaration
        // files. Remaining category and contract failures belong to the selected Package Probe.
        for (const auto &categoryEntry : contributionsIt->second.toObject()) {
            if (!ContribLocator::isValidDottedId(categoryEntry.first)) {
                return Error(Error::InvalidFormat, "contributions contains an invalid category");
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
            package->manifestDeclaration = std::move(desc);
            return package;
        }

        for (const auto &categoryEntry : contributionsIt->second.toObject()) {
            const auto &categoryName = categoryEntry.first;
            auto category = m_synthUnit->category(categoryName);

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
                    !ContribLocator::isValidSegment(contributionIdIt->second.toString())) {
                    return Error(Error::InvalidFormat,
                                 "contribution entry has a missing or invalid id");
                }
                const auto contributionId = contributionIdIt->second.toString();
                if (!contributionIds.insert(contributionId).second) {
                    return Error(Error::InvalidFormat,
                                 "contribution category contains a duplicate id");
                }
                context.locator = ContribLocator(categoryName, contributionId);

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
                    // Absolute paths and paths outside Package root are intentional. Their layout
                    // stability is the Package author's responsibility.
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
                        !ContribLocator::isValidDottedId(interfaceIt->second.toString())) {
                        return Error(Error::InvalidFormat,
                                     "module interface is missing or invalid");
                    }
                    if (variantIt == declaration.end() || !variantIt->second.isString() ||
                        !ContribLocator::isValidDottedId(variantIt->second.toString())) {
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
                        context.imports.reserve(importsIt->second.toArray().size());
                        for (const auto &importValue : importsIt->second.toArray()) {
                            if (!importValue.isObject()) {
                                return Error(Error::InvalidFormat,
                                             "module import must be an object");
                            }
                            const auto &importObject = importValue.toObject();
                            const auto roleIt = importObject.find("role");
                            if (roleIt == importObject.end() || !roleIt->second.isString() ||
                                !isValidImportRole(roleIt->second.toString())) {
                                return Error(Error::InvalidFormat,
                                             "module import requires a valid string role field");
                            }
                            auto importRole = roleIt->second.toString();
                            const auto refIt = importObject.find("ref");
                            if (refIt == importObject.end() || !refIt->second.isString()) {
                                return Error(Error::InvalidFormat,
                                             "module import requires a string ref field");
                            }
                            auto locator = ContribLocator::fromString(refIt->second.toString());
                            if (!locator.isValid()) {
                                return Error(Error::InvalidFormat,
                                             "module import has an invalid ref field");
                            }
                            if (!locator.isLocal() && std::none_of(package->dependencies.begin(),
                                                                   package->dependencies.end(),
                                                                   [&](const auto &dependency) {
                                                                       return dependency.id ==
                                                                              locator.packageId();
                                                                   })) {
                                return Error(Error::InvalidFormat,
                                             "module import references an undeclared dependency");
                            }
                            JsonValue options = JsonObject{};
                            if (const auto it = importObject.find("options");
                                it != importObject.end()) {
                                options = it->second;
                            }
                            // Repeated locators are distinct import instances and array order is
                            // part of the importing contract.
                            auto import = context.addImport(std::move(importRole),
                                                            std::move(locator), std::move(options));
                            if (!import) {
                                return Error(Error::InvalidFormat,
                                             "module import role must be unique");
                            }
                            context.imports.push_back(*import);
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
                auto specPointer = spec.get();
                package->contributions[categoryName].push_back(specPointer);
                package->contributionIndex[categoryName].emplace(contributionId, specPointer);
                package->ownedContributions.push_back(std::move(spec));
            }
        }

        package->manifestDeclaration = std::move(desc);
        return package;
    }

}
