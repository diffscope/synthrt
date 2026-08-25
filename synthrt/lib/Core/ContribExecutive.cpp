#include "ContribExecutive.h"

#include <algorithm>
#include <cassert>

#include "ContribSpec_p.h"
#include "PackageHandle_p.h"
#include "SynthUnit_p.h"

namespace srt {

    ContribExecutive::ContribExecutive(ContribSpec &spec)
        : m_spec(&spec), m_package(spec._impl->package) {
        assert(m_package && m_package->loaded && m_package->synthUnit);
        std::lock_guard<std::recursive_mutex> lock(m_package->synthUnit->_impl->loadMutex);
        m_package->executives.push_back(this);
    }

    ContribExecutive::~ContribExecutive() {
        assert(m_package && m_package->synthUnit);
        std::lock_guard<std::recursive_mutex> lock(m_package->synthUnit->_impl->loadMutex);
        m_destroyingChildren = true;
        for (auto child : m_children) {
            delete child;
        }
        m_children.clear();
        if (m_parent && !m_parent->m_destroyingChildren) {
            auto &siblings = m_parent->m_children;
            const auto childIt =
                std::find_if(siblings.begin(), siblings.end(),
                             [this](const auto sibling) { return sibling == this; });
            assert(childIt != siblings.end());
            // Leave a tombstone because the child is still executing its destructor.
            *childIt = nullptr;
        }
        const auto it = std::find(m_package->executives.begin(), m_package->executives.end(), this);
        assert(it != m_package->executives.end());
        m_package->executives.erase(it);
    }

    ContribSpec &ContribExecutive::spec() const {
        return *m_spec;
    }

    SynthUnit &ContribExecutive::synthUnit() const {
        assert(m_package && m_package->synthUnit);
        return *m_package->synthUnit;
    }

    ContribExecutive::LifecycleState ContribExecutive::lifecycleState() const noexcept {
        return m_state.load(std::memory_order_acquire);
    }

    ContribExecutive *ContribExecutive::parent() const noexcept {
        return m_parent;
    }

    std::vector<ContribExecutive *> ContribExecutive::children() const {
        assert(m_package && m_package->synthUnit);
        std::lock_guard<std::recursive_mutex> lock(m_package->synthUnit->_impl->loadMutex);
        std::vector<ContribExecutive *> result;
        result.reserve(m_children.size());
        for (auto child : m_children) {
            if (child) {
                result.push_back(child);
            }
        }
        return result;
    }

    Expected<ContribExecutive *>
        ContribExecutive::adoptChild(std::unique_ptr<ContribExecutive> child) {
        assert(m_package && m_package->synthUnit);
        std::lock_guard<std::recursive_mutex> lock(m_package->synthUnit->_impl->loadMutex);
        if (!child) {
            return Error(Error::InvalidArgument, "child executive is null");
        }
        if (child.get() == this) {
            return Error(Error::InvalidArgument, "an executive cannot supervise itself");
        }
        if (child->m_package->synthUnit != m_package->synthUnit) {
            return Error(Error::InvalidArgument,
                         "parent and child executives belong to different SynthUnits");
        }
        if (lifecycleState() != LifecycleState::Running ||
            child->lifecycleState() != LifecycleState::Running) {
            return Error(Error::InvalidArgument,
                         "only running executives can form a supervision relation");
        }
        if (child->m_parent) {
            return Error(Error::InvalidArgument, "executive already has a supervising parent");
        }
        for (auto ancestor = this; ancestor; ancestor = ancestor->m_parent) {
            if (ancestor == child.get()) {
                return Error(Error::RecursiveDependency,
                             "executive supervision relation would form a cycle");
            }
        }
        auto result = child.get();
        child->m_parent = this;
        const auto emptyIt = std::find_if(m_children.begin(), m_children.end(),
                                          [](const auto item) { return !item; });
        if (emptyIt != m_children.end()) {
            *emptyIt = child.release();
            return result;
        }
        m_children.push_back(child.release());
        return result;
    }

    Expected<ContribExecutive *>
        ContribExecutive::createChild(std::string_view role,
                                      const ContribRuntimeOptions &runtimeOptions) {
        if (lifecycleState() != LifecycleState::Running) {
            return Error(Error::InvalidArgument, "a stopping executive cannot create children");
        }
        auto import = m_spec->findImport(role);
        if (!import) {
            return Error(Error::InvalidArgument, "executive import role does not exist");
        }
        if (!import->executiveFactory()) {
            return Error(Error::FeatureNotSupported, "import role does not provide an executive");
        }
        auto child = import->executiveFactory()->create(runtimeOptions);
        if (!child) {
            return child.takeError();
        }
        return adoptChild(child.take());
    }

    Expected<void> ContribExecutive::quitForUnload() {
        assert(m_package && m_package->synthUnit);
        std::lock_guard<std::recursive_mutex> lock(m_package->synthUnit->_impl->loadMutex);
        LifecycleState expected = LifecycleState::Running;
        if (!m_state.compare_exchange_strong(expected, LifecycleState::Stopping,
                                             std::memory_order_acq_rel)) {
            return {};
        }
        auto firstResult = quit();
        for (auto child : m_children) {
            if (!child) {
                continue;
            }
            auto result = child->quitForUnload();
            if (firstResult && !result) {
                firstResult = result.takeError();
            }
        }
        return firstResult;
    }

    Expected<void> ContribExecutive::waitForUnload() {
        assert(m_package && m_package->synthUnit);
        std::lock_guard<std::recursive_mutex> lock(m_package->synthUnit->_impl->loadMutex);
        assert(lifecycleState() != LifecycleState::Running);
        if (lifecycleState() == LifecycleState::Stopped) {
            return {};
        }
        Expected<void> firstResult;
        for (auto child : m_children) {
            if (!child) {
                continue;
            }
            auto result = child->waitForUnload();
            if (firstResult && !result) {
                firstResult = result.takeError();
            }
        }
        auto ownResult = wait();
        if (firstResult && !ownResult) {
            firstResult = ownResult.takeError();
        }
        if (firstResult) {
            m_state.store(LifecycleState::Stopped, std::memory_order_release);
        }
        return firstResult;
    }

}
