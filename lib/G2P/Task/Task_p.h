#ifndef SRT_G2P_TASK_TASK_P_H
#define SRT_G2P_TASK_TASK_P_H

#include <shared_mutex>
#include <string>

#include <synthrt/G2P/Task/Task.h>

namespace srt::g2p {

    /// Task::Impl - Task-specific data (composition pattern).
    ///
    /// Unlike LangCore's Task::Impl (which inherits from NamedObject::Impl),
    /// srt::g2p::Task cannot access srt::core::NamedObject::Impl (private to
    /// srt-core). Instead, Task extends NamedObject using its default
    /// constructor, and stores Task-specific data here.
    class Task::Impl {
    public:
        Impl() = default;

        const srt::core::ModuleSpec *spec = nullptr;
        PackageManager *mgr = nullptr;
        int cachedApiLevel = 1;
        std::string config;
        mutable std::shared_mutex mutex;
    };

} // namespace srt::g2p

#endif // SRT_G2P_TASK_TASK_P_H
