#include "TaskHandle.h"

#include "TaskExecution_p.h"

namespace srt {

    TaskHandle::~TaskHandle() = default;

    bool TaskHandle::isActive() const noexcept {
        return m_execution && m_execution->isActive();
    }

    bool TaskHandle::isFinished() const noexcept {
        return m_execution && m_execution->isFinished();
    }

    Expected<void> TaskHandle::stop() {
        return m_execution ? m_execution->stop() : Expected<void>();
    }

    Expected<void> TaskHandle::wait() {
        return m_execution ? m_execution->wait() : Expected<void>();
    }

}
