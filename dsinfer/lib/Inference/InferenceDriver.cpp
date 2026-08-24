#include "InferenceDriver.h"

#include <utility>

namespace ds {

    InferenceDriver::InferenceDriver(std::string name, std::string backend)
        : RuntimeService(IID, std::move(name)), m_backend(std::move(backend)) {
    }

    InferenceDriver::~InferenceDriver() = default;

}
