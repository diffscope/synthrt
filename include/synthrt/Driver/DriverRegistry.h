#ifndef SRT_DRIVER_DRIVERREGISTRY_H
#define SRT_DRIVER_DRIVERREGISTRY_H

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <synthrt/Core/Support/Expected.h>

#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/srt_driver_global.h>

namespace srt::driver {

    /// DriverRegistry - Thread-safe registry of InferenceDriver instances.
    ///
    /// Drivers are registered by a unique name (e.g. "dsdriver") and can be
    /// looked up by name or enumerated. The registry uses a shared_mutex so
    /// that concurrent read-only lookups do not block each other, while write
    /// operations (register/unregister) take an exclusive lock.
    ///
    /// Lock strategy:
    ///   - registerDriver / unregisterDriver: exclusive (unique_lock)
    ///   - driver / driverNames / hasDriver: shared (shared_lock)
    ///
    /// The registry does NOT take ownership of the driver pointers; callers
    /// must keep the driver alive for the duration of its registration.
    class SRT_DRIVER_EXPORT DriverRegistry {
    public:
        DriverRegistry() = default;
        ~DriverRegistry() = default;

        DriverRegistry(const DriverRegistry &) = delete;
        DriverRegistry &operator=(const DriverRegistry &) = delete;

    public:
        /// Registers \p driver under the unique \p name. Returns an error if
        /// the name is already taken.
        srt::core::Expected<void> registerDriver(const std::string &name,
                                                  InferenceDriver *driver);

        /// Unregisters the driver previously registered under \p name.
        /// Returns true if a driver was removed.
        bool unregisterDriver(const std::string &name);

        /// Returns the driver registered under \p name, or nullptr.
        InferenceDriver *driver(const std::string &name) const;

        /// Returns true if \p name is registered.
        bool hasDriver(const std::string &name) const;

        /// Returns all registered driver names.
        std::vector<std::string> driverNames() const;

        /// Returns all registered drivers.
        std::vector<InferenceDriver *> drivers() const;

    private:
        mutable std::shared_mutex m_mtx;
        std::unordered_map<std::string, InferenceDriver *> m_drivers;
    };

}

#endif // SRT_DRIVER_DRIVERREGISTRY_H
