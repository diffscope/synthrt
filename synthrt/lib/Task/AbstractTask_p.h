#ifndef SYNTHRT_ABSTRACTTASK_P_H
#define SYNTHRT_ABSTRACTTASK_P_H

#include <synthrt/Task/AbstractTask.h>

#include "Core/NamedObject_p.h"

namespace srt {

    class AbstractTask::Impl : public NamedObject::Impl {
    public:
        inline Impl(AbstractTask *task) : NamedObject::Impl(task) {
        }

        State state = Idle;
    };

}

#endif // SYNTHRT_ABSTRACTTASK_P_H