#include "PackageHandle.h"
#include "PackageHandle_p.h"

#include <cassert>
#include <utility>

namespace srt {

    PackageHandle::PackageHandle(const PackageHandle &other) = default;

    PackageHandle::PackageHandle(PackageHandle &&other) noexcept = default;

    PackageHandle &PackageHandle::operator=(const PackageHandle &other) = default;

    PackageHandle &PackageHandle::operator=(PackageHandle &&other) noexcept = default;

    PackageHandle::~PackageHandle() = default;

    PackageHandle::operator bool() const noexcept {
        return static_cast<bool>(m_data);
    }

    void PackageHandle::reset() noexcept {
        m_data.reset();
    }

    const std::string &PackageHandle::id() const {
        assert(m_data);
        return m_data->id;
    }

    stdc::VersionNumber PackageHandle::version() const {
        assert(m_data);
        return m_data->version;
    }

    bool PackageHandle::isLoaded() const {
        assert(m_data);
        return m_data->loaded;
    }

    SynthUnit &PackageHandle::synthUnit() const {
        assert(m_data && m_data->synthUnit);
        return *m_data->synthUnit;
    }

    DisplayText PackageHandle::name() const {
        assert(m_data);
        return m_data->name;
    }

    DisplayText PackageHandle::description() const {
        assert(m_data);
        return m_data->description;
    }

    DisplayText PackageHandle::vendor() const {
        assert(m_data);
        return m_data->vendor;
    }

    DisplayText PackageHandle::readme() const {
        assert(m_data);
        return m_data->readme;
    }

    DisplayText PackageHandle::license() const {
        assert(m_data);
        return m_data->license;
    }

    const std::string &PackageHandle::url() const {
        assert(m_data);
        return m_data->url;
    }

    std::vector<ContribSpec *> PackageHandle::contributions(std::string_view category) const {
        assert(m_data);
        const auto it = m_data->contributions.find(category);
        return it == m_data->contributions.end() ? std::vector<ContribSpec *>() : it->second;
    }

    ContribSpec *PackageHandle::contribution(std::string_view category, std::string_view id) const {
        assert(m_data);
        const auto categoryIt = m_data->contributionIndex.find(category);
        if (categoryIt == m_data->contributionIndex.end()) {
            return nullptr;
        }
        const auto contributionIt = categoryIt->second.find(id);
        return contributionIt == categoryIt->second.end() ? nullptr : contributionIt->second;
    }

    ContribSpec *PackageHandle::resolve(const ContribReference &reference) const {
        assert(m_data);
        if (reference.isLocal()) {
            return contribution(reference.category(), reference.contributionId());
        }

        const auto dependency = m_data->dependencyBindings.find(reference.packageId());
        if (dependency == m_data->dependencyBindings.end()) {
            return nullptr;
        }
        return PackageHandle(dependency->second)
            .contribution(reference.category(), reference.contributionId());
    }

    const std::filesystem::path &PackageHandle::path() const {
        assert(m_data);
        return m_data->path;
    }

    stdc::array_view<PackageDependency> PackageHandle::dependencies() const {
        assert(m_data);
        return m_data->dependencies;
    }

    bool PackageHandle::operator==(const PackageHandle &other) const noexcept {
        return m_data == other.m_data;
    }

    bool PackageHandle::operator!=(const PackageHandle &other) const noexcept {
        return !(*this == other);
    }

    PackageHandle::PackageHandle(std::shared_ptr<PackageData> data) : m_data(std::move(data)) {
    }

}
