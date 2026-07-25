#pragma once

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Support/Expected.h>

#include <diffsinger/Bank/dsbank_global.h>

namespace ds::bank {

    class PackageListConfig;

    /// PackageListItemMetadata - Installed package metadata.
    class DSBANK_EXPORT PackageListItemMetadata {
    public:
        inline PackageListItemMetadata() = default;
        inline PackageListItemMetadata(bool hasSinger, std::time_t installedTimestamp);

        inline bool hasSinger() const;
        inline std::time_t installedTimestamp() const;

    protected:
        bool m_hasSinger = false;
        std::time_t m_installedTimestamp = 0;

        friend class PackageListConfig;
    };

    inline PackageListItemMetadata::PackageListItemMetadata(bool hasSinger,
                                                            std::time_t installedTimestamp)
        : m_hasSinger(hasSinger), m_installedTimestamp(installedTimestamp) {
    }

    inline bool PackageListItemMetadata::hasSinger() const {
        return m_hasSinger;
    }

    inline std::time_t PackageListItemMetadata::installedTimestamp() const {
        return m_installedTimestamp;
    }


    /// PackageListItem - Installed package brief information.
    class DSBANK_EXPORT PackageListItem {
    public:
        inline PackageListItem() = default;
        inline PackageListItem(std::string id, stdc::VersionNumber version,
                               std::filesystem::path relativeLocation,
                               PackageListItemMetadata metadata);

        inline const std::string &id() const;
        inline const std::filesystem::path &relativeLocation() const;
        inline const PackageListItemMetadata &metadata() const;

    protected:
        std::string m_id;
        stdc::VersionNumber m_version;
        std::filesystem::path m_relativeLocation;
        PackageListItemMetadata m_metadata;

        friend class PackageListConfig;
    };

    inline PackageListItem::PackageListItem(std::string id, stdc::VersionNumber version,
                                            std::filesystem::path relativeLocation,
                                            PackageListItemMetadata metadata)
        : m_id(std::move(id)), m_version(version), m_relativeLocation(std::move(relativeLocation)),
          m_metadata(std::move(metadata)) {
    }

    inline const std::string &PackageListItem::id() const {
        return m_id;
    }

    inline const std::filesystem::path &PackageListItem::relativeLocation() const {
        return m_relativeLocation;
    }

    inline const PackageListItemMetadata &PackageListItem::metadata() const {
        return m_metadata;
    }


    /// PackageListConfig - Package install directory status configuration file
    /// reader/writer. The on-disk format is a JSON array of objects, each
    /// describing an installed package (\c id, \c relativeLocation, \c metadata).
    class DSBANK_EXPORT PackageListConfig {
    public:
        inline PackageListConfig() = default;
        inline PackageListConfig(std::vector<PackageListItem> packages);

        inline const std::vector<PackageListItem> &packages() const;

    public:
        srt::core::Expected<void> load(const std::filesystem::path &path);
        srt::core::Expected<void> save(const std::filesystem::path &path) const;

    protected:
        std::vector<PackageListItem> m_packages;
    };

    inline PackageListConfig::PackageListConfig(std::vector<PackageListItem> packages)
        : m_packages(std::move(packages)) {
    }

    inline const std::vector<PackageListItem> &PackageListConfig::packages() const {
        return m_packages;
    }

}
