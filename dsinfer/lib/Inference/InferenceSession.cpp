#include "InferenceSession.h"

#include <utility>

namespace ds {

    InferenceSession::InferenceSession() = default;

    InferenceSession::InferenceSession(srt::ContribContract inputContract)
        : ITask(std::move(inputContract)) {
    }

    InferenceSession::~InferenceSession() = default;

}
