#include "ContribExecInstance.h"

#include <algorithm>
#include <cassert>

#include "ContribSpec_p.h"
#include "PackageHandle_p.h"
#include "SynthUnit_p.h"

namespace srt {

    ContribExecInstance::ContribExecInstance(ContribSpec &spec)
        : m_spec(&spec), m_package(spec._impl->package) {
        assert(m_package && m_package->loaded && m_package->synthUnit);
        std::lock_guard<std::recursive_mutex> lock(m_package->synthUnit->_impl->loadMutex);
        m_package->execInstances.push_back(this);
    }

    ContribExecInstance::~ContribExecInstance() {
        assert(m_package && m_package->synthUnit);
        std::lock_guard<std::recursive_mutex> lock(m_package->synthUnit->_impl->loadMutex);
        const auto it =
            std::find(m_package->execInstances.begin(), m_package->execInstances.end(), this);
        assert(it != m_package->execInstances.end());
        m_package->execInstances.erase(it);
    }

    ContribSpec &ContribExecInstance::spec() const {
        return *m_spec;
    }

    SynthUnit &ContribExecInstance::synthUnit() const {
        assert(m_package && m_package->synthUnit);
        return *m_package->synthUnit;
    }

    ContribExecInstance::LifecycleState ContribExecInstance::lifecycleState() const noexcept {
        return m_state.load(std::memory_order_acquire);
    }

    Expected<void> ContribExecInstance::quitForUnload() {
        LifecycleState expected = LifecycleState::Running;
        if (!m_state.compare_exchange_strong(expected, LifecycleState::Stopping,
                                             std::memory_order_acq_rel)) {
            return {};
        }
        return quit();
    }

    Expected<void> ContribExecInstance::waitForUnload() {
        assert(lifecycleState() != LifecycleState::Running);
        if (lifecycleState() == LifecycleState::Stopped) {
            return {};
        }
        auto result = wait();
        if (result) {
            m_state.store(LifecycleState::Stopped, std::memory_order_release);
        }
        return result;
    }

}
