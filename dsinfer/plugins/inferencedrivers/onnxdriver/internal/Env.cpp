#include "Env.h"
#include <stdexcept>

namespace ds::onnxdriver {

    void Env::setDeviceConfig(const DeviceConfig& config) {
        std::unique_lock lock(s_mutex);
        s_deviceConfig = config;
    }

    Env::DeviceConfig Env::getDeviceConfig() {
        std::shared_lock lock(s_mutex);
        return s_deviceConfig;
    }

    int64_t Env::nextId() {
        return ++s_idCounter;
    }
} // namespace ds::onnxdriver