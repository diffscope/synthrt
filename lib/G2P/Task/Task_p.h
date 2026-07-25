#pragma once

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

        const srt::core::ModuleSpec *m_spec = nullptr;
        PackageManager *m_mgr = nullptr;
        int m_cachedApiLevel = 1;
        std::string m_config;
        mutable std::shared_mutex m_mutex;
    };

} // namespace srt::g2p
