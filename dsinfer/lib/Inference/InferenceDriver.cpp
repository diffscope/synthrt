#include "InferenceDriver.h"

#include <utility>

namespace ds {

    InferenceDriver::InferenceDriver(std::string name) : RuntimeService(IID, std::move(name)) {
    }

    InferenceDriver::~InferenceDriver() = default;

}
