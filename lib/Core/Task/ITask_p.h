#pragma once

#include <synthrt/Core/Task/ITask.h>

#include "Core/NamedObject_p.h"

namespace srt::core {

    class ITask::Impl : public NamedObject::Impl {
    public:
        inline Impl(ITask *task) : NamedObject::Impl(task) {
        }

        State m_state = Idle;
    };

}
