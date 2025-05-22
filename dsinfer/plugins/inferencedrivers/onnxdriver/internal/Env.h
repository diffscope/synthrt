#ifndef DSINFER_ONNXDRIVER_ENV_H
#define DSINFER_ONNXDRIVER_ENV_H

#include <atomic>
#include <shared_mutex>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

namespace ds::onnxdriver {

    class Env {
    public:
        struct DeviceConfig {
            DeviceConfig() : ep(Api::Onnx::CPUExecutionProvider), deviceIndex(-1) {
            }
            DeviceConfig(Api::Onnx::ExecutionProvider provider, int index)
                : ep(provider), deviceIndex(index) {
            }

            Api::Onnx::ExecutionProvider ep;
            int deviceIndex;
        };

        // Set/Get the entire device config atomically
        static void setDeviceConfig(const DeviceConfig& config);
        static DeviceConfig getDeviceConfig();
        static int64_t nextId();

    private:
        static inline DeviceConfig s_deviceConfig;
        static inline std::shared_mutex s_mutex;
        static inline std::atomic<int64_t> s_idCounter = 0;
    };

} // namespace ds::onnxdriver

#endif // DSINFER_ONNXDRIVER_ENV_H