#include "PackageRef_p.h"

#include <fstream>
#include <regex>
#include <set>

#include <stdcorelib/path.h>
#include <stdcorelib/stlextra/algorithms.h>

#include "Contribute_p.h"
#include "SynthUnit_p.h"

namespace fs = std::filesystem;

namespace srt {

    PackageData::~PackageData() {
        for (const auto &it : std::as_const(contributes)) {
            for (const auto &it2 : it.second) {
                delete it2.second;
            }
        }
    }

    static bool isValidPackageIdentifier(std::string_view token) {
        static const std::regex re(R"(^[A-Za-z0-9_-]+(?:/[A-Za-z0-9_-]+)*$)");
        return std::regex_match(token.begin(), token.end(), re);
    }

    static Expected<DisplayText> readDisplayTextField(const JsonObject &obj, std::string_view key,
                                                      const fs::path &descPath) {
        auto it = obj.find(std::string(key));
        if (it == obj.end()) {
            return DisplayText();
        }
        auto text = DisplayText::fromJsonValue(it->second)
                        .withContext(Error::InvalidFormat,
                                     stdc::formatN(R"(%1: "%2" field has invalid value)", descPath,
                                                   key));
        if (!text) {
            return text.error();
        }
        return text.take();
    }

    static Expected<DisplayPath> readDisplayPathField(const JsonObject &obj, std::string_view key,
                                                      const fs::path &descPath) {
        auto it = obj.find(std::string(key));
        if (it == obj.end()) {
            return DisplayPath();
        }
        auto path = DisplayPath::fromJsonValue(it->second)
                        .withContext(Error::InvalidFormat,
                                     stdc::formatN(R"(%1: "%2" field has invalid value)", descPath,
                                                   key));
        if (!path) {
            return path.error();
        }
        return path.take();
    }

    Expected<void>
        PackageData::parse(const std::filesystem::path &dir,
                           const std::map<std::string, std::unique_ptr<ContribCategory>,
                                          std::less<>> &categories,
                           stdc::vlarray_base<ContribSpec *> *outContributes) {
        std::string id_;
        stdc::VersionNumber version_;
        DisplayText name_;
        DisplayText vendor_;
        DisplayText description_;
        DisplayPath readme_;
        DisplayPath license_;
        std::string url_;
        stdc::vlarray<PackageDependency> dependencies_;

        stdc::vlarray<ContribSpec *> contributes_;

        // Read desc
        JsonObject obj;
        if (auto exp = readDesc(dir); !exp) {
            return exp.error();
        } else {
            obj = exp.take();
        }

        auto canonicalDir = fs::canonical(dir);
        const auto &descPath = canonicalDir / STDC_TSTR("desc.json");

        {
            const std::set<std::string_view> allowedKeys = {
                "contributes", "dependencies", "description", "id",      "license",
                "name",        "readme",       "url",         "vendor",  "version",
            };
            for (const auto &item : obj) {
                if (!allowedKeys.count(std::string_view(item.first))) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(%1: unknown field "%2")", descPath, item.first),
                    };
                }
            }
        }

        // id
        {
            auto it = obj.find("id");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: missing "id" field)", descPath),
                };
            }
            id_ = it->second.toString();
            if (!isValidPackageIdentifier(id_)) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: "id" field has invalid value)", descPath),
                };
            }
        }
        // version
        {
            auto it = obj.find("version");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: missing "version" field)", descPath),
                };
            }
            const auto parsed = stdc::VersionNumber::fromString(it->second.toString());
            if (!parsed || parsed->isEmpty()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: invalid version)", descPath),
                };
            }
            version_ = *parsed;
        }
        // name
        {
            auto exp = readDisplayTextField(obj, "name", descPath);
            if (!exp) {
                return exp.error();
            }
            name_ = exp.take();
        }
        // vendor
        {
            auto exp = readDisplayTextField(obj, "vendor", descPath);
            if (!exp) {
                return exp.error();
            }
            vendor_ = exp.take();
        }
        // description
        {
            auto exp = readDisplayTextField(obj, "description", descPath);
            if (!exp) {
                return exp.error();
            }
            description_ = exp.take();
        }
        // readme
        {
            auto exp = readDisplayPathField(obj, "readme", descPath);
            if (!exp) {
                return exp.error();
            }
            readme_ = exp.take();
        }
        // license
        {
            auto exp = readDisplayPathField(obj, "license", descPath);
            if (!exp) {
                return exp.error();
            }
            license_ = exp.take();
        }
        // url
        {
            auto it = obj.find("url");
            if (it != obj.end()) {
                url_ = it->second.toString();
            }
        }
        // dependencies
        {
            auto it = obj.find("dependencies");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: missing "dependencies" field)", descPath),
                };
            }
            if (!it->second.isArray()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: "dependencies" field has invalid value)", descPath),
                };
            }

            for (const auto &item : it->second.toArray()) {
                PackageDependency dep;
                if (auto exp = PackageDependency::fromJsonValue(item).withContext(
                        Error::InvalidFormat,
                        stdc::formatN(R"(%1: invalid "dependencies" field entry %2)", descPath,
                                      dependencies_.size() + 1));
                    !exp) {
                    return exp.error();
                } else {
                    dep = exp.take();
                }
                dependencies_.push_back(dep);
            }
        }
        // contributes
        {
            auto it = obj.find("contributes");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: missing "contributes" field)", descPath),
                };
            }
            if (!it->second.isObject()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: "contributes" field has invalid value)", descPath),
                };
            }

            do {
                Error error1;
                const auto &contributesObj = it->second.toObject();

                // Every property is optional. A package contributing one kind and not another says
                // so by leaving the other out, rather than by writing an empty array that carries
                // no information. Unknown properties are still rejected, by the category lookup
                // just below.
                //
                // Core does not know which kinds exist. Requiring particular ones here would mean
                // naming categories that are registered into Core rather than defined by it, and
                // there is no longer any reason to believe the set is the two that SVS defines.
                for (const auto &pair : contributesObj) {
                    const auto &contributeKey = pair.first;
                    auto it2 = categories.find(contributeKey);
                    if (it2 == categories.end()) {
                        error1 = {
                            Error::FeatureNotSupported,
                            stdc::formatN(R"(unknown contribute "%1")", contributeKey),
                        };
                        goto out_failed;
                    }

                    const auto &cc = it2->second;
                    if (!pair.second.isArray()) {
                        error1 = {
                            Error::InvalidFormat,
                            stdc::formatN(
                                R"(contribute "%1" field has invalid value in package manifest)",
                                contributeKey),
                        };
                        goto out_failed;
                    }

                    // Each entry is the package naming a module and saying where it is. The
                    // identifier belongs here rather than in the module's own manifest because it
                    // is what a reference from anywhere else names - it is the package's word for
                    // the module, not a property of the module. Keeping it here is also what lets
                    // a reference be resolved from this file alone, without opening every module.
                    const auto entryError = [&contributeKey](size_t index, const std::string &what) {
                        return Error{
                            Error::InvalidFormat,
                            stdc::formatN(R"(contribute "%1" entry %2 %3 in package manifest)",
                                          contributeKey, index, what),
                        };
                    };

                    std::set<std::string> idSet;
                    size_t index = 0;
                    for (const auto &item : pair.second.toArray()) {
                        index++;
                        if (!item.isObject()) {
                            error1 = entryError(index, "must be an object");
                            goto out_failed;
                        }
                        const auto &entry = item.toObject();
                        {
                            static const std::set<std::string_view> allowedKeys = {"id", "path"};
                            bool unknown = false;
                            for (const auto &field : entry) {
                                if (!allowedKeys.count(std::string_view(field.first))) {
                                    error1 = entryError(
                                        index, stdc::formatN(R"(has unknown field "%1")",
                                                             field.first));
                                    unknown = true;
                                    break;
                                }
                            }
                            if (unknown) {
                                goto out_failed;
                            }
                        }

                        auto idIt = entry.find("id");
                        if (idIt == entry.end() || !idIt->second.isString() ||
                            !ContribLocator::isValidSegment(idIt->second.toString())) {
                            error1 = entryError(index, R"(has a missing or invalid "id" field)");
                            goto out_failed;
                        }
                        auto contributeId = idIt->second.toString();

                        // Caught before the module is opened, so a package that names the same
                        // module twice is rejected without the cost of reading it twice.
                        if (!idSet.emplace(contributeId).second) {
                            error1 = {
                                Error::InvalidFormat,
                                stdc::formatN(R"(contribute "%1" object has duplicated id "%2")",
                                              contributeKey, contributeId),
                            };
                            goto out_failed;
                        }

                        auto pathIt = entry.find("path");
                        if (pathIt == entry.end() || !pathIt->second.isString()) {
                            error1 = entryError(index, R"(has a missing or invalid "path" field)");
                            goto out_failed;
                        }

                        auto contribute = cc->parseSpec(canonicalDir, pathIt->second);
                        if (!contribute) {
                            error1 = contribute.error();
                            goto out_failed;
                        }
                        contribute.get()->_impl->id = std::move(contributeId);
                        contributes_.push_back(contribute.get());
                    }
                }

                break;

            out_failed:
                stdc::delete_all(contributes_);
                return error1;
            } while (false);
        }

        path = canonicalDir;
        id = std::move(id_);
        version = version_;
        name = std::move(name_);
        vendor = std::move(vendor_);
        description = std::move(description_);
        readme = std::move(readme_);
        license = std::move(license_);
        url = std::move(url_);
        dependencies = std::move(dependencies_);
        *outContributes = std::move(contributes_);
        return Expected<void>();
    }

    Expected<JsonObject> PackageData::readDesc(const std::filesystem::path &dir) {
        const auto &descPath = dir / STDC_TSTR("desc.json");
        std::ifstream file(descPath);
        if (!file.is_open()) {
            return Error{
                Error::FileNotOpen,
                stdc::formatN(R"("%1": failed to open package manifest)", descPath),
            };
        }

        std::stringstream ss;
        ss << file.rdbuf();

        stdc::json::ParseError error2;
        auto root = JsonValue::fromJson(ss.str(), true, &error2);
        if (error2) {
            return Error{
                Error::InvalidFormat,
                stdc::formatN(R"("%1": invalid package manifest format: %2)", descPath,
                              error2.message()),
            };
        }
        if (!root.isObject()) {
            return Error{
                Error::InvalidFormat,
                stdc::formatN(R"("%1": invalid package manifest format: not an object)", descPath),
            };
        }
        return root.toObject();
    }

    static PackageData &staticEmptyPackageData() {
        static PackageData empty(nullptr);
        return empty;
    }

    Expected<PackageDependency> PackageDependency::fromJsonValue(const JsonValue &val) {
        if (!val.isObject()) {
            return Error{
                Error::InvalidFormat,
                R"(invalid data type)",
            };
        }

        auto obj = val.toObject();
        {
            const std::set<std::string_view> allowedKeys = {"id", "version"};
            for (const auto &item : obj) {
                if (!allowedKeys.count(std::string_view(item.first))) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(unknown field "%1")", item.first),
                    };
                }
            }
        }

        auto it = obj.find("id");
        if (it == obj.end()) {
            return Error{
                Error::InvalidFormat,
                R"(missing "id" field)",
            };
        }
        std::string_view id = it->second.toString();
        if (!isValidPackageIdentifier(id)) {
            return Error{
                Error::InvalidFormat,
                R"(invalid id)",
            };
        }

        it = obj.find("version");
        if (it == obj.end()) {
            return Error{
                Error::InvalidFormat,
                R"(missing "version" field)",
            };
        }

        const auto parsed = stdc::VersionNumber::fromString(it->second.toString());
        if (!parsed || parsed->isEmpty()) {
            return Error{
                Error::InvalidFormat,
                R"(invalid version)",
            };
        }

        PackageDependency res;
        res.id = id;
        res.version = *parsed;
        return res;
    }

    PackageRef::PackageRef() : _data(&staticEmptyPackageData()) {
    }

    PackageRef::~PackageRef() = default;

    bool PackageRef::close() {
        if (!_data->su) {
            return true;
        }
        if (!static_cast<SynthUnit::Impl *>(_data->su->_impl.get())->close(_data)) {
            return false;
        }
        _data = &staticEmptyPackageData();
        return true;
    }

    const std::string &PackageRef::id() const {
        return _data->id;
    }

    stdc::VersionNumber PackageRef::version() const {
        return _data->version;
    }

    DisplayText PackageRef::name() const {
        return _data->name;
    }

    DisplayText PackageRef::description() const {
        return _data->description;
    }

    DisplayText PackageRef::vendor() const {
        return _data->vendor;
    }

    DisplayPath PackageRef::readme() const {
        return _data->readme;
    }

    DisplayPath PackageRef::license() const {
        return _data->license;
    }

    const std::string &PackageRef::url() const {
        return _data->url;
    }

    std::vector<ContribSpec *> PackageRef::contributes(const std::string_view &category) const {
        auto &contributes = _data->contributes;
        auto it = contributes.find(category);
        if (it == contributes.end()) {
            return {};
        }

        std::vector<ContribSpec *> res;
        const auto &map2 = it->second;
        res.reserve(map2.size());
        for (const auto &pair : std::as_const(map2)) {
            res.push_back(pair.second);
        }
        return res;
    }

    ContribSpec *PackageRef::contribute(const std::string_view &category,
                                        const std::string_view &id) const {
        auto &contributes = _data->contributes;
        auto it = contributes.find(category);
        if (it == contributes.end()) {
            return nullptr;
        }

        const auto &map2 = it->second;
        auto it2 = map2.find(id);
        if (it2 == map2.end()) {
            return nullptr;
        }
        return it2->second;
    }

    const std::filesystem::path &PackageRef::path() const {
        return _data->path;
    }

    stdc::array_view<PackageDependency> PackageRef::dependencies() const {
        return _data->dependencies;
    }

    Error PackageRef::error() const {
        return _data->err;
    }

    bool PackageRef::isLoaded() const {
        return _data->loaded;
    }

    SynthUnit *PackageRef::SU() const {
        return _data->su;
    }

    void ScopedPackageRef::forceClose() {
        if (!close()) {
            _data = &staticEmptyPackageData();
        }
    }

}
