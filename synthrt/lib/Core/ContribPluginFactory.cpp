#include "ContribPluginFactory_p.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <stdcorelib/path.h>
#include <stdcorelib/support/json.h>
#include <stdcorelib/support/sharedlibrary.h>

#include "ContribInterpreterPlugin.h"
#include "ContribReference.h"

namespace fs = std::filesystem;

namespace srt {

    namespace {

        constexpr const char *manifestName = "plugin.json";

        bool manifestProvidesInterpreter(const stdc::json::Value &manifest,
                                         std::string_view interfaceName, std::string_view variant,
                                         int level) {
            if (!manifest.isObject()) {
                return false;
            }

            const auto &name = manifest["name"];
            if (!name.isString() || !ContribReference::isValidSegment(name.toString())) {
                return false;
            }

            const auto &metadata = manifest["metadata"];
            if (!metadata.isObject()) {
                return false;
            }
            const auto &interpreters = metadata["interpreters"];
            if (!interpreters.isArray() || interpreters.toArray().empty()) {
                return false;
            }

            std::set<std::tuple<std::string, std::string, int64_t>> declarations;
            bool matches = false;
            for (const auto &value : interpreters.toArray()) {
                if (!value.isObject()) {
                    return false;
                }

                const auto &declaredInterface = value["interface"];
                const auto &declaredVariant = value["variant"];
                const auto &declaredLevel = value["level"];
                if (!declaredInterface.isString() || !declaredVariant.isString() ||
                    !declaredLevel.isInt() ||
                    !ContribReference::isValidDottedId(declaredInterface.toString()) ||
                    !ContribReference::isValidDottedId(declaredVariant.toString())) {
                    return false;
                }

                const auto levelValue = declaredLevel.toInt();
                if (levelValue <= 0 || levelValue > std::numeric_limits<int>::max()) {
                    return false;
                }

                const auto declaration = std::make_tuple(declaredInterface.toString(),
                                                         declaredVariant.toString(), levelValue);
                if (!declarations.insert(declaration).second) {
                    return false;
                }
                if (declaredInterface.toString() == interfaceName &&
                    declaredVariant.toString() == variant && levelValue == level) {
                    matches = true;
                }
            }
            return matches;
        }

        std::optional<fs::path> resolveLibraryName(const fs::path &directory,
                                                   std::string_view name) {
            const auto requestedPath = stdc::path::from_utf8(name);
            if (requestedPath.empty() || requestedPath.is_absolute() ||
                requestedPath.has_parent_path()) {
                return std::nullopt;
            }

            const auto baseName = requestedPath.filename();
            constexpr std::array<std::string_view, 2> prefixes{"", "lib"};
            constexpr std::array<std::string_view, 2> suffixes{
                "",
#ifdef _WIN32
                ".dll",
#elif defined(__APPLE__)
                ".dylib",
#else
                ".so",
#endif
            };

            for (const auto prefix : prefixes) {
                for (const auto suffix : suffixes) {
                    auto candidateName = std::string(prefix);
                    candidateName += stdc::path::to_utf8(baseName);
                    candidateName += suffix;
                    const auto candidate = directory / stdc::path::from_utf8(candidateName);
                    std::error_code error;
                    if (fs::is_regular_file(candidate, error) &&
                        stdc::SharedLibrary::isLibrary(candidate)) {
                        return candidate;
                    }
                }
            }
            return std::nullopt;
        }

    }

    stdc::plugin::PluginLoader *
        ContribPluginFactory::findInterpreter(std::string_view iid, std::string_view interfaceName,
                                              std::string_view variant, int level) const {
        for (auto *loader : plugins(iid)) {
            if (manifestProvidesInterpreter(loader->manifest(), interfaceName, variant, level)) {
                return loader;
            }
        }
        return nullptr;
    }

    Expected<ContribInterpreter *>
        ContribPluginFactory::loadInterpreter(stdc::plugin::PluginLoader *loader) {
        if (!loader) {
            return Error(Error::InvalidArgument, "interpreter plugin loader must not be null");
        }

        const auto cached = m_interpreters.find(loader);
        if (cached != m_interpreters.end()) {
            return cached->second.get();
        }

        if (!loader->load()) {
            return Error(Error::InvalidFormat, "failed to load contribution interpreter plugin: " +
                                                   loader->errorMessage());
        }

        auto *plugin = static_cast<ContribInterpreterPlugin *>(loader->plugin());
        auto result = plugin->create();
        if (!result) {
            return result.takeError().withContext("failed to create contribution interpreter");
        }
        auto interpreter = result.take();
        if (!interpreter) {
            return Error(Error::InvalidFormat,
                         "contribution interpreter plugin returned a null interpreter");
        }

        auto *value = interpreter.get();
        m_interpreters.emplace(loader, std::move(interpreter));
        return value;
    }

    bool ContribPluginFactory::scanPluginPaths(
        const fs::path &path, std::vector<std::filesystem::path> *pluginPaths) const {
        std::error_code error;
        fs::directory_iterator iterator(path, error);
        if (error) {
            return false;
        }

        const fs::directory_iterator end;
        while (iterator != end) {
            if (iterator->is_directory(error) && !error &&
                fs::is_regular_file(iterator->path() / manifestName, error) && !error) {
                pluginPaths->push_back(iterator->path());
            }
            error.clear();
            iterator.increment(error);
            if (error) {
                return false;
            }
        }

        std::sort(pluginPaths->begin(), pluginPaths->end(), [](const auto &LHS, const auto &RHS) {
            return stdc::path::to_utf8(LHS.filename()) < stdc::path::to_utf8(RHS.filename());
        });
        return true;
    }

    bool ContribPluginFactory::resolvePluginPath(const fs::path &path, fs::path *pluginPath,
                                                 std::optional<fs::path> *manifestPath) const {
        const auto manifest = path / manifestName;
        std::ifstream file(manifest);
        if (!file.is_open()) {
            return false;
        }

        std::stringstream stream;
        stream << file.rdbuf();
        stdc::json::ParseError parseError;
        const auto root = stdc::json::Value::fromJson(stream.str(), true, &parseError);
        if (parseError || !root.isObject()) {
            return false;
        }

        const auto &name = root["name"];
        if (!name.isString() || name.toString().empty()) {
            return false;
        }

        auto resolvedPath = resolveLibraryName(path, name.toString());
        if (!resolvedPath) {
            return false;
        }

        *pluginPath = std::move(*resolvedPath);
        *manifestPath = manifest;
        return true;
    }

}
