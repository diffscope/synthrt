#include "InferenceDriver.h"

#include <utility>

namespace ds {

    InferenceDriver::InferenceDriver(std::string backend)
        : RuntimeService(IID, std::move(backend)) {
    }

    InferenceDriver::~InferenceDriver() = default;

}
