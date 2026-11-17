#include <dsinfer/Support/ErrorCode.h>

#include <memory>
#include <string>

#include <pitch/PitchInterpreter.h>
#include <pitch/PitchInference.h>
#include <pitch/PitchTask.h>

namespace Contract = ds::Api::Pitch::L1;
using TestInterpreter = ds::PitchInterpreter;

#define BOOST_TEST_MAIN
#include "../TestSupport.h"

namespace {
    class OtherInit : public srt::TaskInitArgs {
    public:
        OtherInit() : TaskInitArgs("com.example.Other", 1) {
        }
    };
    class OtherInput : public srt::TaskStartInput {
    public:
        OtherInput() : TaskStartInput("com.example.Other", 1) {
        }
    };
}

BOOST_AUTO_TEST_CASE(task_rejects_invalid_initialization_and_uninitialized_execution) {
    ds::test::DeclaredInference declaration;
    ds::PitchInference executive(declaration.spec());
    ds::PitchTask task(executive);
    OtherInit args;
    OtherInput input;
    auto initialized = task.initialize(static_cast<const srt::TaskInitArgs &>(args));
    BOOST_REQUIRE(!initialized);
    BOOST_CHECK(initialized.error().code() == srt::Error::InvalidArgument);
    auto result = task.start(static_cast<const srt::TaskStartInput &>(input));
    BOOST_REQUIRE(!result);
    BOOST_CHECK(result.error().code() == ds::ErrorCode::NotInitialized);
    bool called = false;
    auto async = task.startAsync(std::shared_ptr<const srt::TaskStartInput>(),
                                 [&called](auto) { called = true; });
    BOOST_REQUIRE(!async);
    BOOST_CHECK(async.error().code() == srt::Error::InvalidArgument);
    BOOST_CHECK(!called);
    BOOST_REQUIRE(task.waitForFinished());
}
