#ifndef SRT_CORE_TASK_ITASK_P_H
#define SRT_CORE_TASK_ITASK_P_H

#include <synthrt/Core/Task/ITask.h>

#include "Core/NamedObject_p.h"

namespace srt::core {

    class ITask::Impl : public NamedObject::Impl {
    public:
        inline Impl(ITask *task) : NamedObject::Impl(task) {
        }

        State state = Idle;
    };

}

#endif // SRT_CORE_TASK_ITASK_P_H
