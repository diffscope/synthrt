#include "InferenceSession.h"

#include <utility>

namespace ds {

    InferenceSession::InferenceSession() = default;

    InferenceSession::InferenceSession(std::string inputType, int inputVersion)
        : ITask(std::move(inputType), inputVersion) {
    }

    InferenceSession::~InferenceSession() = default;

}
