#pragma once

#include <atomic>
#include <shared_mutex>

#include <synthrt/Driver/onnx/OnnxDriverApi.h>

namespace srt::driver::onnx {

    class Env {
    public:
        struct DeviceConfig {
            DeviceConfig() : ep(CPUExecutionProvider), deviceIndex(-1) {
            }
            DeviceConfig(ExecutionProvider provider, int index)
                : ep(provider), deviceIndex(index) {
            }

            ExecutionProvider ep;
            int deviceIndex;
        };

        // Set/Get the entire device config atomically
        static void setDeviceConfig(const DeviceConfig &config);
        static DeviceConfig getDeviceConfig();
        static int64_t nextId();

    private:
        static inline DeviceConfig s_deviceConfig;
        static inline std::shared_mutex s_mutex;
        static inline std::atomic<int64_t> s_idCounter = 0;
    };

} // namespace srt::driver::onnx
