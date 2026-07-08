#ifndef SRT_G2P_TASK_TASK_H
#define SRT_G2P_TASK_TASK_H

#include <filesystem>
#include <memory>
#include <string>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Task/ITask.h>
#include <synthrt/G2P/srt_g2p_global.h>
#include <synthrt/G2P/Support/Error.h>

namespace srt::g2p {

    class PackageManager;

    /// ModuleSpec alias — ModuleSpec is defined in srt::core; alias it here
    /// for use in G2P-facing signatures (matches the alias in Package.h).
    using ModuleSpec = srt::core::ModuleSpec;

    /// TaskInfoBase - alias to srt::core::TaskInfoBase for G2P task info types.
    using TaskInfoBase = srt::core::TaskInfoBase;
    using TaskInitArgs = srt::core::TaskInitArgs;
    using TaskInput = srt::core::TaskStartInput;
    using TaskResult = srt::core::TaskResult;
    using TaskConfiguration = srt::core::TaskConfiguration;

    /// Task - G2P task base class (no async interface).
    ///
    /// Migrated from LangCore::Task. Unlike srt::core::ITask (which has
    /// startAsync/stop), srt::g2p::Task is synchronous: callers invoke
    /// start() and block for the result. This matches the G2P use case where
    /// conversions are short-lived.
    class SRT_G2P_EXPORT Task : public srt::core::NamedObject {
    public:
        Task();
        explicit Task(const ModuleSpec *spec);
        ~Task() override;

        virtual int apiLevel() const = 0;

        virtual srt::core::Expected<void> initialize() = 0;

        virtual srt::core::Expected<srt::core::NO<TaskResult>> start(
            const srt::core::NO<TaskInput> &input) = 0;

        const ModuleSpec *spec() const;
        PackageManager *Mgr() const;

        srt::core::Expected<srt::core::NO<srt::core::NamedObject>> getObject(
            const std::string &category, const std::string &id) const;

        /// 获取完整配置（JSON 字符串）
        virtual std::string getConfig() const;

    protected:
        /// 初始化配置（自动加载配置，子类在 initialize() 中调用）
        srt::core::Expected<void> initializeConfig();

        /// 加载配置（从默认配置路径加载）
        srt::core::Expected<std::string> loadConfig() const;

        /// Set the owning manager (called by PackageManager during task creation).
        void setMgr(PackageManager *mgr);

    private:
        class Impl;
        std::unique_ptr<Impl> d;

        friend class PackageManager;
    };

    /// SessionTask - Provides a basic interface for the memory image of an AI model.
    class SRT_G2P_EXPORT SessionTask : public Task {
    public:
        virtual srt::core::Expected<void> open(const std::filesystem::path &path,
                                                const srt::core::NO<TaskInitArgs> &args) = 0;
        virtual srt::core::Expected<void> close() = 0;
        virtual bool isOpen() const = 0;

        virtual int64_t id() const = 0;
    };

} // namespace srt::g2p

#endif // SRT_G2P_TASK_TASK_H
