#include "DriverContext.h"

#include <utility>

namespace ds::onnxdriver {

    DriverContext::DriverContext(std::shared_ptr<OnnxRuntime> runtime,
                                 Api::Onnx::ExecutionProvider executionProvider, int deviceIndex)
        : m_runtime(std::move(runtime)), m_executionProvider(executionProvider),
          m_deviceIndex(deviceIndex) {
    }

    OnnxRuntime &DriverContext::runtime() const noexcept {
        return *m_runtime;
    }

    Api::Onnx::ExecutionProvider DriverContext::executionProvider() const noexcept {
        return m_executionProvider;
    }

    int DriverContext::deviceIndex() const noexcept {
        return m_deviceIndex;
    }

    int64_t DriverContext::nextSessionId() noexcept {
        return ++m_sessionId;
    }

    SessionSystem &DriverContext::sessionSystem() noexcept {
        return m_sessionSystem;
    }

}
