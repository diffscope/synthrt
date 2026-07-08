#include <synthrt/G2P/Package/Package.h>

#include <fstream>
#include <sstream>

#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

#include "Core/PackageManager_p.h"
#include "Package/Package_p.h"

namespace fs = std::filesystem;

namespace srt::g2p {

    // ============================================================================
    // PackageData
    // ============================================================================

    PackageData::~PackageData() {
        for (const auto &[_, byId] : std::as_const(moduleSpecs)) {
            for (const auto &[__, spec] : byId) {
                delete spec;
            }
        }
    }

    srt::core::Expected<void> PackageData::parse(
        const std::filesystem::path &dir,
        const std::map<std::string, srt::core::ModuleCategory *, std::less<>> &categories,
        llvm::SmallVectorImpl<srt::core::ModuleSpec *> *outModules) {
        auto manifestPath = dir / "package.json";
        if (!fs::exists(manifestPath)) {
            return Error(Error::FileSystemError,
                         stdc::formatN("Package manifest not found: %1", manifestPath));
        }

        auto expObj = readDesc(manifestPath);
        if (!expObj) return expObj.error();
        auto root = expObj.take();

        // Required: packageId
        {
            auto it = root.find("packageId");
            if (it == root.end() || !it->second.isString())
                return Error(Error::ConfigError, "package.json missing required field: packageId");
            id = it->second.toString();
        }

        // Required: version
        {
            auto it = root.find("version");
            if (it == root.end() || !it->second.isString())
                return Error(Error::ConfigError, "package.json missing required field: version");
            version = stdc::VersionNumber::fromString(it->second.toString());
            if (version.isEmpty())
                return Error(Error::ConfigError, "package.json version is empty/zero");
        }

        // Optional: compatVersion (default to version)
        {
            auto it = root.find("compatVersion");
            if (it != root.end() && it->second.isString()) {
                compatVersion = stdc::VersionNumber::fromString(it->second.toString());
            } else {
                compatVersion = version;
            }
        }

        // Optional: level
        {
            auto it = root.find("level");
            if (it != root.end() && it->second.isNumber())
                level = it->second.toInt();
        }

        // Optional: vendor / copyright / description / url
        auto readDisplayText = [&](const std::string &key) -> DisplayText {
            DisplayText dt;
            auto it = root.find(key);
            if (it == root.end()) return dt;
            if (it->second.isString()) {
                dt.set("en", it->second.toString());
            } else if (it->second.isObject()) {
                for (const auto &[lang, val] : it->second.toObject()) {
                    if (val.isString()) dt.set(lang, val.toString());
                }
            }
            return dt;
        };
        description = readDisplayText("description");
        vendor = readDisplayText("vendor");
        copyright = readDisplayText("copyright");
        {
            auto it = root.find("url");
            if (it != root.end() && it->second.isString())
                url = it->second.toString();
        }
        // readme
        {
            auto readmePath = dir / "README.md";
            if (fs::exists(readmePath))
                readme = readmePath;
        }

        path = dir;

        // Process modules array
        auto it = root.find("modules");
        if (it == root.end() || !it->second.isObject()) {
            loaded = true;
            return {};
        }
        const auto &modulesObj = it->second.toObject();

        for (const auto &[categoryName, moduleListVal] : modulesObj) {
            if (!moduleListVal.isArray()) continue;
            const auto &moduleList = moduleListVal.toArray();

            auto catIt = categories.find(categoryName);
            if (catIt == categories.end()) {
                return Error(Error::NotImplementedError,
                             stdc::formatN("Unknown module category: %1", categoryName));
            }
            auto *category = catIt->second;

            for (const auto &moduleEntry : moduleList) {
                if (!moduleEntry.isObject()) continue;
                const auto &entryObj = moduleEntry.toObject();

                // moduleId
                auto idIt = entryObj.find("moduleId");
                if (idIt == entryObj.end() || !idIt->second.isString())
                    continue;
                std::string moduleId = idIt->second.toString();

                // Parse via category
                auto specExp = category->parseSpec(dir, moduleEntry);
                if (!specExp) continue;
                auto *spec = specExp.take();

                // Register in package
                moduleSpecs[categoryName][moduleId] = spec;
                if (outModules) {
                    outModules->push_back(spec);
                }
            }
        }

        loaded = true;
        return {};
    }

    srt::core::Expected<srt::core::JsonObject> PackageData::readDesc(const std::filesystem::path &descPath) {
        // std::ifstream is a third-party boundary; convert exceptions to Error (ROBUST-02).
        try {
            const std::ifstream file(descPath);
            if (!file.is_open()) {
                return Error{
                    Error::FileSystemError,
                    stdc::formatN(R"("%1": failed to open package manifest)", descPath),
                };
            }

            std::stringstream ss;
            ss << file.rdbuf();

            std::string parseError;
            const auto root = srt::core::JsonValue::fromJson(ss.str(), true, &parseError);
            if (!parseError.empty()) {
                return Error{
                    Error::ConfigError,
                    stdc::formatN(R"("%1": invalid package manifest format: %2)", descPath, parseError),
                };
            }
            if (!root.isObject()) {
                return Error{
                    Error::ConfigError,
                    stdc::formatN(R"("%1": invalid package manifest format: not an object)", descPath),
                };
            }
            return root.toObject();
        } catch (const std::exception &e) {
            return Error{
                Error::FileSystemError,
                stdc::formatN(R"("%1": error reading package manifest: %2)", descPath, e.what()),
            };
        }
    }

    // ============================================================================
    // Package
    // ============================================================================

    static PackageData &staticEmptyPackageData() {
        static PackageData empty(nullptr);
        return empty;
    }

    Package::Package() : _data(&staticEmptyPackageData()) {}

    Package::~Package() = default;

    bool Package::close() {
        if (!_data->mgr) {
            return true;
        }
        // P3.2+: call PackageManager::Impl::close(_data) to decrement refcount
        // and delete if ref == 0. For P3.1, since open() is stubbed, no real
        // package is ever loaded, so close() is a no-op.
        _data = &staticEmptyPackageData();
        return true;
    }

    const std::string &Package::id() const { return _data->id; }

    stdc::VersionNumber Package::version() const { return _data->version; }

    stdc::VersionNumber Package::compatVersion() const { return _data->compatVersion; }

    DisplayText Package::description() const { return _data->description; }

    DisplayText Package::vendor() const { return _data->vendor; }

    DisplayText Package::copyright() const { return _data->copyright; }

    const std::filesystem::path &Package::readme() const { return _data->readme; }

    const std::string &Package::url() const { return _data->url; }

    std::vector<srt::core::ModuleSpec *> Package::moduleSpecs(const std::string_view &category) const {
        auto &modules = _data->moduleSpecs;
        const auto it = modules.find(category);
        if (it == modules.end()) {
            return {};
        }

        std::vector<srt::core::ModuleSpec *> res;
        const auto &byId = it->second;
        res.reserve(byId.size());
        for (const auto &[_, spec] : std::as_const(byId)) {
            res.push_back(spec);
        }
        return res;
    }

    srt::core::ModuleSpec *Package::moduleSpec(const std::string_view &category,
                                                const std::string_view &id) const {
        auto &modules = _data->moduleSpecs;
        const auto it = modules.find(category);
        if (it == modules.end()) {
            return nullptr;
        }

        const auto &byId = it->second;
        const auto it2 = byId.find(id);
        if (it2 == byId.end()) {
            return nullptr;
        }
        return it2->second;
    }

    const std::filesystem::path &Package::path() const { return _data->path; }

    Error Package::error() const { return _data->err; }

    bool Package::isLoaded() const { return _data->loaded; }

    PackageManager *Package::Mgr() const { return _data->mgr; }

    // ============================================================================
    // ScopedPackageRef
    // ============================================================================

    void ScopedPackageRef::forceClose() {
        if (!close()) {
            _data = &staticEmptyPackageData();
        }
    }

} // namespace srt::g2p
