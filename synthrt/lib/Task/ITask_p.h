#ifndef SYNTHRT_ITask_P_H
#define SYNTHRT_ITask_P_H

#include <synthrt/Task/ITask.h>

#include "Core/NamedObject_p.h"

namespace srt {

    class ITask::Impl : public NamedObject::Impl {
    public:
        Impl() = default;

        State state = Idle;
    };

}

#endif // SYNTHRT_ITask_P_H
