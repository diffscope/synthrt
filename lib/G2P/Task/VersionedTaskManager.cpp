// VersionedTaskManager - out-of-line implementation.
//
// MSVC does not emit symbols for inline methods of exported classes in the
// DLL unless a key function is defined out-of-line. Moving the destructor
// and substantial method bodies here forces emission of all symbols.

#include <synthrt/G2P/Task/VersionedTaskManager.h>

namespace srt::g2p {

    VersionedTaskManager::VersionedTaskManager(const ModuleSpec *spec)
        : _spec(spec), _currentLevel(spec ? spec->apiLevel() : 1) {}

    VersionedTaskManager::~VersionedTaskManager() = default;

    srt::core::Expected<void> VersionedTaskManager::initialize() {
        if (_unsupportedLevel > 0)
            return Error(Error::NotImplementedError,
                         "apiLevel " + std::to_string(_unsupportedLevel) +
                             " is not supported by this plugin");
        if (!_impl)
            return Error(Error::NullPointerError,
                         "VersionedTaskManager: impl not set (call setImpl() first)");
        if (_implMinLevel > 0 &&
            (_currentLevel < _implMinLevel || _currentLevel > _implMaxLevel))
            return Error(Error::NotImplementedError,
                         "apiLevel " + std::to_string(_currentLevel) +
                             " not in supported range [" + std::to_string(_implMinLevel) +
                             ", " + std::to_string(_implMaxLevel) + "]");
        return _impl->initialize();
    }

    srt::core::Expected<srt::core::NO<TaskResult>>
    VersionedTaskManager::start(const srt::core::NO<TaskInput> &input) {
        if (!_impl)
            return Error(Error::NullPointerError,
                         "VersionedTaskManager: impl not set (call setImpl() first)");
        return _impl->start(input);
    }

    std::string VersionedTaskManager::getConfig() const {
        if (!_impl)
            return {};
        return _impl->getConfig();
    }

} // namespace srt::g2p
