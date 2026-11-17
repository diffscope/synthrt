#include <memory>

#include <dsinfer/Inference/InferenceSession.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {
    class InitArgs : public ds::InferenceSessionInitArgs {
    public:
        InitArgs() : InferenceSessionInitArgs("other.backend", 3) {
        }
    };
    class Input : public ds::InferenceSessionStartInput {
    public:
        Input() : InferenceSessionStartInput("other.backend", 3) {
        }
    };
    class Result : public ds::InferenceSessionResult {
    public:
        Result() : InferenceSessionResult("other.backend", 3) {
        }
    };
    class OpenArgs : public ds::InferenceSessionOpenArgs {
    public:
        OpenArgs() : InferenceSessionOpenArgs("other.backend", 3) {
        }
    };
}

BOOST_AUTO_TEST_CASE(session_payload_bases_preserve_non_onnx_contracts) {
    InitArgs args;
    Input input;
    std::unique_ptr<srt::TaskResult> result = std::make_unique<Result>();
    const srt::TaskPayload *payloads[] = {&args, &input, result.get()};
    for (const auto payload : payloads) {
        BOOST_CHECK_EQUAL(payload->type(), "other.backend");
        BOOST_CHECK_EQUAL(payload->version(), 3);
    }
    OpenArgs open;
    const ds::InferenceDriverPayload &view = open;
    BOOST_CHECK_EQUAL(view.type(), "other.backend");
    BOOST_CHECK_EQUAL(view.version(), 3);
    BOOST_CHECK(view.as<OpenArgs>() == &open);
}
