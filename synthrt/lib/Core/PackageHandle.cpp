#include "PackageHandle.h"
#include "PackageHandle_p.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "ContribCategory_p.h"
#include "ContribExecutive.h"
#include "ContribImportBinding.h"
#include "ContribSpec_p.h"
#include "Logging.h"
#include "SynthUnit_p.h"

namespace srt {

    PackageData::~PackageData() {
        unload();
    }

    void PackageData::unload() {
        if (!synthUnit) {
            return;
        }
        SynthUnit *unit = nullptr;
        std::vector<ContribImportBinding *> bindings;
        {
            std::lock_guard<std::recursive_mutex> lock(synthUnit->_impl->loadMutex);
            if (released) {
                return;
            }
            released = true;
            if (!executives.empty()) {
                logCategory().srtFatal(
                    "Package %1 is being released while %2 executives still exist", id,
                    executives.size());
            }
            unit = synthUnit;
            invalidated.store(true, std::memory_order_release);
            loaded = false;
            synthUnit = nullptr;
            // Close every binding and unregister the contribution before waiting. The lock is then
            // released so a binding wait can use ordinary SynthUnit queries without deadlocking.
            for (const auto &categoryEntry : contributions) {
                for (auto spec : categoryEntry.second) {
                    for (auto &import : spec->_impl->imports) {
                        if (auto binding = import.binding()) {
                            binding->closeForUnload();
                            bindings.push_back(binding);
                        }
                    }
                }
            }
            for (const auto &categoryEntry : contributions) {
                auto category = unit->category(categoryEntry.first);
                if (!category) {
                    continue;
                }
                auto &registered = category->_impl->contributions;
                for (auto contribution : categoryEntry.second) {
                    registered.erase(
                        std::remove(registered.begin(), registered.end(), contribution),
                        registered.end());
                }
            }
        }
        for (auto binding : bindings) {
            const auto result = binding->waitForUnload();
            if (!result) {
                logCategory().srtFatal("an import binding failed to stop during Package unload: %1",
                                       result.error().toString());
            }
        }
    }

    PackageHandle::PackageHandle(const PackageHandle &other) = default;

    PackageHandle::PackageHandle(PackageHandle &&other) noexcept = default;

    PackageHandle &PackageHandle::operator=(const PackageHandle &other) = default;

    PackageHandle &PackageHandle::operator=(PackageHandle &&other) noexcept = default;

    PackageHandle::~PackageHandle() = default;

    PackageHandle::operator bool() const noexcept {
        return m_data && !m_data->invalidated.load(std::memory_order_acquire);
    }

    void PackageHandle::reset() noexcept {
        m_data.reset();
    }

    const std::string &PackageHandle::id() const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        return m_data->id;
    }

    stdc::VersionNumber PackageHandle::version() const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        return m_data->version;
    }

    stdc::VersionNumber PackageHandle::compatVersion() const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        return m_data->compatVersion;
    }

    int PackageHandle::runtimeLevel() const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        return m_data->runtimeLevel;
    }

    const JsonObject &PackageHandle::manifestDeclaration() const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        return m_data->manifestDeclaration;
    }

    bool PackageHandle::isLoaded() const {
        return m_data && !m_data->invalidated.load(std::memory_order_acquire) && m_data->loaded;
    }

    SynthUnit &PackageHandle::synthUnit() const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire) && m_data->synthUnit);
        return *m_data->synthUnit;
    }

    DisplayText PackageHandle::name() const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        return m_data->name;
    }

    DisplayText PackageHandle::description() const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        return m_data->description;
    }

    DisplayText PackageHandle::vendor() const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        return m_data->vendor;
    }

    DisplayText PackageHandle::readme() const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        return m_data->readme;
    }

    DisplayText PackageHandle::license() const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        return m_data->license;
    }

    const std::string &PackageHandle::url() const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        return m_data->url;
    }

    std::vector<ContribSpec *> PackageHandle::contributions(std::string_view category) const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        const auto it = m_data->contributions.find(category);
        return it == m_data->contributions.end() ? std::vector<ContribSpec *>() : it->second;
    }

    ContribSpec *PackageHandle::contribution(std::string_view category, std::string_view id) const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        const auto categoryIt = m_data->contributionIndex.find(category);
        if (categoryIt == m_data->contributionIndex.end()) {
            return nullptr;
        }
        const auto contributionIt = categoryIt->second.find(id);
        return contributionIt == categoryIt->second.end() ? nullptr : contributionIt->second;
    }

    ContribSpec *PackageHandle::resolve(const ContribLocator &locator) const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        if (locator.isLocal()) {
            return contribution(locator.category(), locator.contributionId());
        }

        const auto dependency = m_data->dependencyBindings.find(locator.packageId());
        if (dependency == m_data->dependencyBindings.end()) {
            return nullptr;
        }
        return PackageHandle(dependency->second)
            .contribution(locator.category(), locator.contributionId());
    }

    const std::filesystem::path &PackageHandle::path() const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
        return m_data->path;
    }

    stdc::array_view<PackageDependency> PackageHandle::dependencies() const {
        assert(m_data && !m_data->invalidated.load(std::memory_order_acquire));
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
