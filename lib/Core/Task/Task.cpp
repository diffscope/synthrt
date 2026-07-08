#include <synthrt/Core/Task/Task.h>

namespace srt::core {

    Task::Task() = default;

    Task::Task(const ModuleSpec *spec) : m_spec(spec) {
    }

    Task::~Task() = default;

    SessionTask::SessionTask() = default;

    SessionTask::~SessionTask() = default;

    SessionFactory::SessionFactory() = default;

    SessionFactory::~SessionFactory() = default;

} // namespace srt::core
