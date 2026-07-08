#include <synthrt/Driver/DriverRegistry.h>

namespace srt::driver {

    srt::core::Expected<void> DriverRegistry::registerDriver(const std::string &name,
                                                             InferenceDriver *driver) {
        if (!driver) {
            return srt::core::Error{
                srt::core::Error::InvalidArgument,
                "driver is null",
            };
        }
        std::unique_lock<std::shared_mutex> lock(m_mtx);
        if (m_drivers.find(name) != m_drivers.end()) {
            return srt::core::Error{
                srt::core::Error::InvalidArgument,
                "driver name already registered: " + name,
            };
        }
        m_drivers.emplace(name, driver);
        return srt::core::Expected<void>();
    }

    bool DriverRegistry::unregisterDriver(const std::string &name) {
        std::unique_lock<std::shared_mutex> lock(m_mtx);
        auto it = m_drivers.find(name);
        if (it == m_drivers.end()) {
            return false;
        }
        m_drivers.erase(it);
        return true;
    }

    InferenceDriver *DriverRegistry::driver(const std::string &name) const {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        auto it = m_drivers.find(name);
        if (it == m_drivers.end()) {
            return nullptr;
        }
        return it->second;
    }

    bool DriverRegistry::hasDriver(const std::string &name) const {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        return m_drivers.find(name) != m_drivers.end();
    }

    std::vector<std::string> DriverRegistry::driverNames() const {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        std::vector<std::string> names;
        names.reserve(m_drivers.size());
        for (const auto &pair : m_drivers) {
            names.push_back(pair.first);
        }
        return names;
    }

    std::vector<InferenceDriver *> DriverRegistry::drivers() const {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        std::vector<InferenceDriver *> result;
        result.reserve(m_drivers.size());
        for (const auto &pair : m_drivers) {
            result.push_back(pair.second);
        }
        return result;
    }

}
