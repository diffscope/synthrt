#ifndef DSINFER_PACKAGELISTCONFIG_H
#define DSINFER_PACKAGELISTCONFIG_H

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>
#include <synthrt/Support/Expected.h>
#include <dsinfer/dsinfer_global.h>

namespace ds {

    class PackageListConfig;

    /// PackageListItemMetadata - Installed package metadata.
    class PackageListItemMetadata {
    public:
        inline PackageListItemMetadata() = default;
        inline PackageListItemMetadata(bool hasSinger, std::time_t installedTimestamp);

        inline bool hasSinger() const;
        inline std::time_t installedTimestamp() const;

    protected:
        bool _hasSinger = false;
        std::time_t _installedTimestamp = 0;

        friend class PackageListConfig;
    };

    inline PackageListItemMetadata::PackageListItemMetadata(bool hasSinger,
                                                            std::time_t installedTimestamp)
        : _hasSinger(hasSinger), _installedTimestamp(installedTimestamp) {
    }

    inline bool PackageListItemMetadata::hasSinger() const {
        return _hasSinger;
    }

    inline std::time_t PackageListItemMetadata::installedTimestamp() const {
        return _installedTimestamp;
    }


    /// PackageListItem - Installed package brief information.
    class PackageListItem {
    public:
        inline PackageListItem() = default;
        inline PackageListItem(std::string id, stdc::VersionNumber version,
                               std::filesystem::path relativeLocation,
                               PackageListItemMetadata metadata);

        inline const std::string &id() const;
        inline const std::filesystem::path &relativeLocation() const;
        inline const PackageListItemMetadata &metadata() const;

    protected:
        std::string _id;
        stdc::VersionNumber _version;
        std::filesystem::path _relativeLocation;
        PackageListItemMetadata _metadata;

        friend class PackageListConfig;
    };

    inline PackageListItem::PackageListItem(std::string id, stdc::VersionNumber version,
                                            std::filesystem::path relativeLocation,
                                            PackageListItemMetadata metadata)
        : _id(std::move(id)), _version(version), _relativeLocation(std::move(relativeLocation)),
          _metadata(std::move(metadata)) {
    }

    inline const std::string &PackageListItem::id() const {
        return _id;
    }

    inline const std::filesystem::path &PackageListItem::relativeLocation() const {
        return _relativeLocation;
    }

    inline const PackageListItemMetadata &PackageListItem::metadata() const {
        return _metadata;
    }


    /// PackageListConfig - Package install directory status configuration file reader/writer.
    class PackageListConfig {
    public:
        inline PackageListConfig() = default;
        inline PackageListConfig(std::vector<PackageListItem> packages);

        inline const std::vector<PackageListItem> &packages() const;

    public:
        DSINFER_EXPORT srt::Expected<void> load(const std::filesystem::path &path);
        DSINFER_EXPORT srt::Expected<void> save(const std::filesystem::path &path) const;

    protected:
        std::vector<PackageListItem> _packages;
    };

    inline PackageListConfig::PackageListConfig(std::vector<PackageListItem> packages)
        : _packages(std::move(packages)) {
    }

    inline const std::vector<PackageListItem> &PackageListConfig::packages() const {
        return _packages;
    }

}

#endif // DSINFER_PACKAGELISTCONFIG_H