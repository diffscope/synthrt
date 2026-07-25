#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Task/ITask.h>

namespace srt::core {

    using TaskInput = TaskStartInput;

    /// Synchronous module task base. Async/stop/state remain on ITask for callers
    /// that need host-managed execution.
    class SRT_CORE_EXPORT Task : public NamedObject {
    public:
        Task();
        explicit Task(const ModuleSpec *spec);
        ~Task() override;

        virtual int apiLevel() const = 0;
        virtual Expected<void> initialize() = 0;
        virtual Expected<NO<TaskResult>> start(const NO<TaskInput> &input) = 0;

        const ModuleSpec *spec() const { return m_spec; }

        Expected<NO<NamedObject>> getObject(const std::string &category,
                                            const std::string &id) const {
            if (!m_spec || !m_spec->runtime()) {
                return Error(Error::SessionError, "runtime is not available");
            }
            const auto moduleCategory = m_spec->runtime()->moduleCategory(category);
            if (!moduleCategory) {
                return Error(Error::SessionError, "could not find category: " + category);
            }
            const auto object = moduleCategory->getFirstObject(id);
            if (!object) {
                return Error(Error::SessionError, "could not find id: " + id);
            }
            return object;
        }

    protected:
        const ModuleSpec *m_spec = nullptr;
    };

    class SRT_CORE_EXPORT SessionTask : public Task {
    public:
        SessionTask();
        using Task::Task;
        ~SessionTask() override;

        virtual Expected<void> open(const std::filesystem::path &path,
                                    const NO<TaskInitArgs> &args) = 0;
        virtual Expected<void> close() = 0;
        virtual bool isOpen() const = 0;
        virtual int64_t id() const = 0;
    };

    class SRT_CORE_EXPORT SessionFactory : public NamedObject {
    public:
        SessionFactory();
        ~SessionFactory() override;

        virtual std::string arch() const = 0;
        virtual std::string backend() const = 0;
        virtual Expected<void> initialize(const NO<TaskInitArgs> &args) = 0;
        virtual NO<SessionTask> createSession() = 0;
    };

} // namespace srt::core
