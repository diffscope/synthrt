#include <fstream>
#include <sstream>
#include <utility>

#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <synthrt/Core/Support/JSON.h>

#include <diffsinger/Bank/PackageListConfig.h>

using srt::core::Error;
using srt::core::Expected;
using srt::core::JsonArray;
using srt::core::JsonObject;
using srt::core::JsonValue;

namespace ds::bank {

    // Parses "<packageId>[<version>]" and returns true on success.
    static bool parsePackageIdVersion(const std::string &token, std::string &package,
                                      stdc::VersionNumber &version) {
        auto openBracket = token.find('[');
        if (openBracket == std::string::npos) {
            return false;
        }
        if (token.empty() || token.back() != ']') {
            return false;
        }
        package = token.substr(0, openBracket);
        if (package.empty()) {
            return false;
        }
        auto versionStr = token.substr(openBracket + 1, token.size() - openBracket - 2);
        version = stdc::VersionNumber::fromString(versionStr).value_or(stdc::VersionNumber());
        return !version.isEmpty();
    }

    Expected<void> PackageListConfig::load(const std::filesystem::path &path) {
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
                std::string package;
                stdc::VersionNumber version;
                if (!parsePackageIdVersion(id, package, version)) {
                    continue;
                }
                pkg.m_id = std::move(package);
                pkg.m_version = std::move(version);
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
                pkg.m_relativeLocation = stdc::path::from_utf8(path_);
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
                    metadata_.m_hasSinger = it3->second.toBool();
                } while (false);

                // installedTimestamp (optional)
                do {
                    auto it3 = metadataObj.find("installedTimestamp");
                    if (it3 == metadataObj.end() || !it3->second.isInt()) {
                        break;
                    }
                    metadata_.m_installedTimestamp =
                        static_cast<std::time_t>(it3->second.toInt());
                } while (false);

                pkg.m_metadata = std::move(metadata_);
            }
            pkgs.emplace_back(pkg);
        }

        m_packages = std::move(pkgs);
        return Expected<void>();
    }

    Expected<void> PackageListConfig::save(const std::filesystem::path &path) const {
        JsonArray packagesArr;
        for (const auto &packageItem : m_packages) {
            JsonObject pkgObj;

            // id
            pkgObj["id"] =
                stdc::formatN("%1[%2]", packageItem.m_id, packageItem.m_version.toString());

            // relativeLocation
            pkgObj["relativeLocation"] = stdc::path::to_utf8(packageItem.m_relativeLocation);

            // metadata
            {
                JsonObject metadataObj;
                const auto &metadata = packageItem.m_metadata;

                // hasSinger
                metadataObj["hasSinger"] = metadata.m_hasSinger;

                // installedTimestamp
                const auto installedTimestamp = static_cast<int64_t>(metadata.m_installedTimestamp);
                if (installedTimestamp != 0) {
                    metadataObj["installedTimestamp"] = installedTimestamp;
                }

                pkgObj["metadata"] = std::move(metadataObj);
            }

            packagesArr.emplace_back(pkgObj);
        }

        std::ofstream file(path);
        if (!file.is_open()) {
            return Error{
                Error::FileNotOpen,
                stdc::formatN(R"(%1: failed to create package list configuration)", path),
            };
        }

        auto data = JsonValue(packagesArr).toJson(4);
        file.write(data.data(), std::streamsize(data.size()));
        return Expected<void>();
    }

}
