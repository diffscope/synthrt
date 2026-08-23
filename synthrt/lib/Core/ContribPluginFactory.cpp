#include "ContribPluginFactory_p.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

#include <stdcorelib/path.h>
#include <stdcorelib/support/json.h>
#include <stdcorelib/support/sharedlibrary.h>

namespace fs = std::filesystem;

namespace srt {

    namespace {

        constexpr const char *manifestName = "plugin.json";

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
