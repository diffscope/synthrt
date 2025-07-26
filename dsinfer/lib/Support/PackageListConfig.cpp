#include "PackageListConfig.h"

#include <fstream>

#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <synthrt/Support/JSON.h>
#include <synthrt/Core/Contribute.h>

using srt::JsonArray;
using srt::JsonObject;
using srt::JsonValue;
using srt::Error;
using srt::Expected;

namespace ds {

    srt::Expected<void> PackageListConfig::load(const std::filesystem::path &path) {
        std::string id_;
        std::string arch_;

        // Read configuration
        JsonArray configArr;
        {
            std::ifstream file(path);
            if (!file.is_open()) {
                return Error{
                    Error::FileNotOpen,
                    stdc::formatN(R"(%1: failed to open package list configuration)", path),
                };
            }

            std::stringstream ss;
            ss << file.rdbuf();

            std::string error2;
            auto root = JsonValue::fromJson(ss.str(), true, &error2);
            if (!error2.empty()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: invalid package list configuration format: %2)", path,
                                  error2),
                };
            }
            if (!root.isArray()) {
                return Error{
                    Error::InvalidFormat,
                    stdc::formatN(R"(%1: invalid package list configuration format)", path),
                };
            }
            configArr = root.toArray();
        }

        std::vector<PackageListItem> pkgs;
        pkgs.reserve(configArr.size());

        // Get attributes
        for (const auto &item : configArr) {
            if (!item.isObject()) {
                continue;
            }

            auto packageObj = item.toObject();
            PackageListItem pkg;

            // id[version]
            {
                auto it2 = packageObj.find("id");
                if (it2 == packageObj.end() || !it2->second.isString()) {
                    continue;
                }
                std::string id = it2->second.toString();
                if (id.empty()) {
                    continue;
                }
                auto identifier = srt::ContribLocator::fromString(id);
                if (!identifier.package().empty() && !identifier.version().isEmpty() &&
                    identifier.id().empty()) {
                    pkg._id = identifier.package();
                    pkg._version = identifier.version();
                } else {
                    continue;
                }
            }
            // relativeLocation
            {
                auto it2 = packageObj.find("relativeLocation");
                if (it2 == packageObj.end() || !it2->second.isString()) {
                    continue;
                }
                std::string path_ = it2->second.toString();
                if (path_.empty()) {
                    continue;
                }
                pkg._relativeLocation = stdc::path::from_utf8(path_);
            }
            // metadata
            {
                auto it2 = packageObj.find("metadata");
                if (it2 == packageObj.end() || !it2->second.isObject()) {
                    continue;
                }
                const auto &metadataObj = it2->second.toObject();
                PackageListItemMetadata metadata_;

                // hasSinger (optional)
                do {
                    auto it3 = metadataObj.find("hasSinger");
                    if (it3 == metadataObj.end() || !it3->second.isBool()) {
                        break;
                    }
                    metadata_._hasSinger = it3->second.toBool();
                } while (false);

                // installedTimestamp (optional)
                do {
                    auto it3 = metadataObj.find("installedTimestamp");
                    if (it3 == metadataObj.end() || !it3->second.isInt()) {
                        break;
                    }
                    metadata_._installedTimestamp = it3->second.toInt();
                } while (false);

                pkg._metadata = std::move(metadata_);
            }
            pkgs.emplace_back(pkg);
        }

        _packages = std::move(pkgs);
        return Expected<void>();
    }

    srt::Expected<void> PackageListConfig::save(const std::filesystem::path &path) const {
        JsonObject obj;

        // packages
        {
            JsonArray packagesArr;
            for (const auto &packageItem : std::as_const(_packages)) {
                JsonObject pkgObj;

                // id
                pkgObj["id"] =
                    stdc::formatN("%1[%2]", packageItem._id, packageItem._version.toString());

                // relativeLocation
                pkgObj["relativeLocation"] = stdc::path::to_utf8(packageItem._relativeLocation);

                // metadata
                {
                    JsonObject metadataObj;
                    const auto &metadata = packageItem._metadata;

                    // contributes
                    metadataObj["hasSinger"] = metadata._hasSinger;

                    // installedTimestamp
                    const auto installedTimestamp =
                        static_cast<int64_t>(metadata._installedTimestamp);
                    if (installedTimestamp != 0) {
                        metadataObj["installedTimestamp"] = installedTimestamp;
                    }

                    pkgObj["metadata"] = std::move(metadataObj);
                }

                packagesArr.emplace_back(pkgObj);
            }
            obj["packages"] = std::move(packagesArr);
        }

        std::ofstream file(path);
        if (!file.is_open()) {
            return Error{
                Error::FileNotOpen,
                stdc::formatN(R"(%1: failed to create package list configuration)", path),
            };
        }

        auto data = JsonValue(obj).toString();
        file.write(data.data(), std::streamsize(data.size()));
        return Expected<void>();
    }

}