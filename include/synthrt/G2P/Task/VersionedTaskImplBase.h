#ifndef SRT_G2P_TASK_VERSIONEDTASKIMPLBASE_H
#define SRT_G2P_TASK_VERSIONEDTASKIMPLBASE_H

#include <string>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/srt_g2p_global.h>
#include <synthrt/G2P/Task/Task.h>

namespace srt::g2p {

    /// VersionedTaskImplBase - Abstract base class for multi-version task
    /// implementations.
    ///
    /// Migrated from LangCore::VersionedTaskImplBase. Provides a stable
    /// contract for plugin implementations supporting multiple API levels.
    class SRT_G2P_EXPORT VersionedTaskImplBase {
    public:
        virtual ~VersionedTaskImplBase();

        /// Initialize the task implementation.
        virtual srt::core::Expected<void> initialize() = 0;

        /// Execute the task.
        virtual srt::core::Expected<srt::core::NO<TaskResult>>
        start(const srt::core::NO<TaskInput> &input) = 0;

        /// Get the current configuration (JSON string).
        virtual std::string getConfig() const = 0;
    };

} // namespace srt::g2p

#endif // SRT_G2P_TASK_VERSIONEDTASKIMPLBASE_H
