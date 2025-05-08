#ifndef SYNTHRT_ABSTRACTTASK_P_H
#define SYNTHRT_ABSTRACTTASK_P_H

#include <synthrt/Task/AbstractTask.h>

#include "Core/Object_p.h"

namespace srt {

    class AbstractTask::Impl : public Object::Impl {
    public:
        Impl(AbstractTask *task) : Object::Impl(task) {
        }

        State state = Idle;
    };

}

#endif // SYNTHRT_ABSTRACTTASK_P_H