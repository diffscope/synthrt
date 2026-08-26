#include "InferenceDriver.h"

#include <utility>

#include "InferenceDriverPlugin.h"

namespace ds {

    InferenceDriver::InferenceDriver(std::string backend)
        : RuntimeService(InferenceDriverPlugin::IID, std::move(backend)) {
    }

    InferenceDriver::~InferenceDriver() = default;

}
