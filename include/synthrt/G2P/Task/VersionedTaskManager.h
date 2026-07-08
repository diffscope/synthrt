#ifndef SRT_G2P_TASK_VERSIONEDTASKMANAGER_H
#define SRT_G2P_TASK_VERSIONEDTASKMANAGER_H

#include <memory>
#include <string>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/srt_g2p_global.h>
#include <synthrt/G2P/Support/Error.h>
#include <synthrt/G2P/Task/Task.h>
#include <synthrt/G2P/Task/VersionedTaskImplBase.h>

namespace srt::g2p {

    /// VersionedTaskManager - Multi-version task management helper.
    ///
    /// Migrated from LangCore::VersionedTaskManager. Holds a
    /// VersionedTaskImplBase implementation and delegates initialize/start/
    /// getConfig. Plugins select the implementation in their constructor
    /// based on spec->apiLevel() and call setImpl().
    class SRT_G2P_EXPORT VersionedTaskManager {
    public:
        explicit VersionedTaskManager(const ModuleSpec *spec);
        ~VersionedTaskManager();

        int currentLevel() const { return _currentLevel; }
        const ModuleSpec *spec() const { return _spec; }
        VersionedTaskImplBase *impl() const { return _impl.get(); }

        void setImpl(std::unique_ptr<VersionedTaskImplBase> impl) {
            _impl = std::move(impl);
        }

        /// setImpl overload with Level range validation (A-8, ARCH-02):
        /// initialize() will verify _currentLevel is within [minLevel, maxLevel].
        void setImpl(std::unique_ptr<VersionedTaskImplBase> impl, int minLevel, int maxLevel) {
            _impl = std::move(impl);
            _implMinLevel = minLevel;
            _implMaxLevel = maxLevel;
        }

        /// Mark the current Level as unsupported by this plugin (A-8):
        /// used in the default branch of multi-version plugin switches;
        /// initialize() will return NotImplementedError instead of silent degradation.
        void markLevelUnsupported(int level) { _unsupportedLevel = level; }

        srt::core::Expected<void> initialize();

        srt::core::Expected<srt::core::NO<TaskResult>> start(const srt::core::NO<TaskInput> &input);

        std::string getConfig() const;

    private:
        const ModuleSpec *_spec;
        int _currentLevel;
        std::unique_ptr<VersionedTaskImplBase> _impl;
        int _implMinLevel = 0;     ///< 0 = not set (no range check)
        int _implMaxLevel = 0;
        int _unsupportedLevel = 0; ///< 0 = supported; >0 = unsupported Level value
    };

} // namespace srt::g2p

/// SRT_G2P_TASK_IMPLEMENT - Generate standard delegation methods for a Task
/// using VersionedTaskManager (single-version case).
///
/// Usage (single version):
///   SRT_G2P_TASK_IMPLEMENT(MyTask, Internal::V1::MyTaskImpl)
///
/// Generates: constructor, destructor, apiLevel, initialize, start, getConfig.
/// The constructor automatically creates the specified Impl class.
///
/// For multi-version plugins, write the constructor manually (using switch to
/// select Impl), then use SRT_G2P_TASK_IMPLEMENT_METHODS to generate only
/// the delegation methods.
#define SRT_G2P_TASK_IMPLEMENT(TaskClass, ImplClass)                                                 \
    TaskClass::TaskClass(const ::srt::g2p::ModuleSpec *spec)                                         \
        : ::srt::g2p::Task(spec), _manager(spec) {                                                   \
        _manager.setImpl(std::make_unique<ImplClass>(spec));                                         \
    }                                                                                                \
    SRT_G2P_TASK_IMPLEMENT_METHODS(TaskClass)

/// SRT_G2P_TASK_IMPLEMENT_METHODS - Generate only delegation methods
/// (without constructor). For multi-version plugins that write the constructor
/// manually, then invoke this macro.
#define SRT_G2P_TASK_IMPLEMENT_METHODS(TaskClass)                                                    \
    TaskClass::~TaskClass() = default;                                                               \
                                                                                                     \
    int TaskClass::apiLevel() const {                                                                \
        return _manager.currentLevel();                                                              \
    }                                                                                                \
                                                                                                     \
    ::srt::core::Expected<void> TaskClass::initialize() {                                            \
        return _manager.initialize();                                                                \
    }                                                                                                \
                                                                                                     \
    ::srt::core::Expected<::srt::core::NO<::srt::g2p::TaskResult>>                                   \
    TaskClass::start(const ::srt::core::NO<::srt::g2p::TaskInput> &input) {                          \
        return _manager.start(input);                                                                \
    }                                                                                                \
                                                                                                     \
    std::string TaskClass::getConfig() const {                                                       \
        return _manager.getConfig();                                                                 \
    }

#endif // SRT_G2P_TASK_VERSIONEDTASKMANAGER_H
