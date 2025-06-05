#include "PackageRef_p.h"

#include <fstream>
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

    static bool readDependency(const JsonValue &val, PackageDependency *out,
                               std::string *errorMessage) {
        if (val.isString()) {
            auto identifier = ContribLocator::fromString(val.toString());
            if (!identifier.package().empty() && !identifier.version().isEmpty() &&
                identifier.id().empty()) {
                PackageDependency res;
                res.id = identifier.package();
                res.version = identifier.version();
                *out = std::move(res);
                return true;
            }
            *errorMessage = R"(invalid id)";
            return false;
        }

        if (!val.isObject()) {
            *errorMessage = R"(invalid data type)";
            return false;
        }

        std::string id;
        stdc::VersionNumber version;
        bool required = true;

        auto obj = val.toObject();
        auto it = obj.find("id");
        if (it == obj.end()) {
            *errorMessage = R"(missing "id" field)";
            return false;
        }
        id = it->second.toString();
        if (id.empty()) {
            *errorMessage = R"(invalid id)";
            return false;
        }

        it = obj.find("version");
        if (it != obj.end()) {
            version = stdc::VersionNumber::fromString(it->second.toString());
        }

        it = obj.find("required");
        if (it != obj.end() && !it->second.toBool()) {
            required = false;
        }

        PackageDependency res(required);
        res.id = std::move(id);
        res.version = version;
        *out = std::move(res);
        return true;
    }

    Expected<void>
        PackageData::parse(const std::filesystem::path &dir,
                           const std::map<std::string, ContribCategory *, std::less<>> &categories,
                           std::vector<ContribSpec *> *outContributes) {
        std::string id_;
        stdc::VersionNumber version_;
        stdc::VersionNumber compatVersion_;
        DisplayText vendor_;
        DisplayText copyright_;
        DisplayText description_;
        fs::path readme_;
        std::string url_;
        std::vector<PackageDependency> dependencies_;

        std::vector<ContribSpec *> contributes_;

        // Read desc
        JsonObject obj;
        if (auto exp = readDesc(dir); !exp) {
            return exp.error();
        } else {
            obj = exp.take();
        }

        auto canonicalDir = fs::canonical(dir);
        const auto &descPath = canonicalDir / _TSTR("desc.json");

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
            if (!ContribLocator::isValidLocator(id_)) {
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
            version_ = stdc::VersionNumber::fromString(it->second.toString());
            if (version_.isEmpty()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: invalid version)", descPath),
                };
            }
        }
        // compatVersion
        {
            auto it = obj.find("compatVersion");
            if (it != obj.end()) {
                compatVersion_ = stdc::VersionNumber::fromString(it->second.toString());
                if (compatVersion_ > version_) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(%1: invalid compat version)", descPath),
                    };
                }
            } else {
                compatVersion_ = version_;
            }
        }
        // vendor
        {
            auto it = obj.find("vendor");
            if (it != obj.end()) {
                vendor_ = it->second;
            }
        }
        // copyright
        {
            auto it = obj.find("copyright");
            if (it != obj.end()) {
                copyright_ = it->second;
            }
        }
        // description
        {
            auto it = obj.find("description");
            if (it != obj.end()) {
                description_ = it->second;
            }
        }
        // readme
        {
            auto it = obj.find("readme");
            if (it != obj.end()) {
                readme_ = stdc::path::from_utf8(it->second.toString());
            }
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
            if (it != obj.end()) {
                if (!it->second.isArray()) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(%1: "dependencies" field has invalid value)", descPath),
                    };
                }

                for (const auto &item : it->second.toArray()) {
                    std::string errorMessage;
                    PackageDependency dep;
                    if (!readDependency(item, &dep, &errorMessage)) {
                        return Error{
                            Error::InvalidFormat,
                            stdc::formatN(R"(%1: invalid "dependencies" field entry %2: %3)",
                                          descPath, dependencies_.size() + 1, errorMessage),
                        };
                    }
                    dependencies_.push_back(dep);
                }
            }
        }
        // contributes
        {
            auto it = obj.find("contributes");
            if (it != obj.end()) {
                if (!it->second.isObject()) {
                    return Error{
                        Error::InvalidFormat,
                        R"("contributes" field has invalid value in package manifest)",
                    };
                }
            }

            do {
                Error error1;
                for (const auto &pair : it->second.toObject()) {
                    const auto &contributeKey = pair.first;
                    auto it2 = categories.find(contributeKey);
                    if (it2 == categories.end()) {
                        error1 = {
                            Error::FeatureNotSupported,
                            stdc::formatN(R"(unknown contribute "%1")", contributeKey),
                        };
                        goto out_failed;
                    }

                    const auto &cate = it2->second;
                    if (!pair.second.isArray()) {
                        error1 = {
                            Error::InvalidFormat,
                            stdc::formatN(
                                R"(contribute "%1" field has invalid value in package manifest)",
                                contributeKey),
                        };
                        goto out_failed;
                    }

                    std::set<std::string_view> idSet;
                    for (const auto &item : pair.second.toArray()) {
                        auto contribute = cate->parseSpec(canonicalDir, item);
                        if (!contribute) {
                            error1 = contribute.error();
                            goto out_failed;
                        }
                        contributes_.push_back(contribute.get());

                        // Check id
                        const auto &contributeId = contribute.get()->id();
                        if (idSet.count(contributeId)) {
                            error1 = {
                                Error::InvalidFormat,
                                stdc::formatN(R"(contribute "%1" object has duplicated id "%2")",
                                              pair.first, contributeId),
                            };
                            goto out_failed;
                        }
                        idSet.emplace(contributeId);
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
        compatVersion = compatVersion_;
        vendor = std::move(vendor_);
        copyright = std::move(copyright_);
        description = std::move(description_);
        readme = std::move(readme_);
        url = std::move(url_);
        dependencies = std::move(dependencies_);
        *outContributes = std::move(contributes_);
        return {};
    }

    Expected<JsonObject> PackageData::readDesc(const std::filesystem::path &dir) {
        const auto &descPath = dir / _TSTR("desc.json");
        std::ifstream file(descPath);
        if (!file.is_open()) {
            return Error{
                Error::FileNotFound,
                stdc::formatN(R"("%1": failed to open package manifest)", descPath),
            };
        }

        std::stringstream ss;
        ss << file.rdbuf();

        std::string error2;
        auto root = JsonValue::fromJson(ss.str(), true, &error2);
        if (!error2.empty()) {
            return Error{
                Error::InvalidFormat,
                stdc::formatN(R"("%1": invalid package manifest format: %2)", descPath, error2),
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

    stdc::VersionNumber PackageRef::compatVersion() const {
        return _data->compatVersion;
    }

    DisplayText PackageRef::description() const {
        return _data->description;
    }

    DisplayText PackageRef::vendor() const {
        return _data->vendor;
    }

    DisplayText PackageRef::copyright() const {
        return _data->copyright;
    }

    std::filesystem::path PackageRef::readme() const {
        return _data->readme;
    }

    std::string_view PackageRef::url() const {
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

    std::filesystem::path PackageRef::path() const {
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