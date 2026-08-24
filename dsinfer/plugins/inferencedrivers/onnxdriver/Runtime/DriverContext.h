#ifndef DSINFER_ONNXDRIVER_DRIVERCONTEXT_H
#define DSINFER_ONNXDRIVER_DRIVERCONTEXT_H

#include <atomic>
#include <cstdint>
#include <memory>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

#include "OnnxRuntime.h"
#include "Session/SessionSystem.h"

namespace ds::onnxdriver {

    /// Holds immutable driver configuration and state shared by all of its sessions.
    class DriverContext {
    public:
        DriverContext(std::shared_ptr<OnnxRuntime> runtime,
                      Api::Onnx::ExecutionProvider executionProvider, int deviceIndex);

        /// Returns the ONNX Runtime used by this driver.
        OnnxRuntime &runtime() const noexcept;

        /// Returns the configured execution provider.
        Api::Onnx::ExecutionProvider executionProvider() const noexcept;

        /// Returns the configured provider device index.
        int deviceIndex() const noexcept;

        /// Returns an identifier unique among sessions created by this driver.
        int64_t nextSessionId() noexcept;

        /// Returns the model image cache owned by this driver.
        SessionSystem &sessionSystem() noexcept;

    private:
        // Declared before the cache so runtime objects outlive every cached session image.
        std::shared_ptr<OnnxRuntime> m_runtime;
        SessionSystem m_sessionSystem;
        Api::Onnx::ExecutionProvider m_executionProvider;
        int m_deviceIndex;
        std::atomic<int64_t> m_sessionId = 0;
    };

}

#endif // DSINFER_ONNXDRIVER_DRIVERCONTEXT_H
