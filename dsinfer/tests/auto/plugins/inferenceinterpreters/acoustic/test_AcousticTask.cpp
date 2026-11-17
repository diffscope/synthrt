#include <memory>
#include <string>

#include <acoustic/AcousticInterpreter.h>
#include <acoustic/AcousticInference.h>
#include <acoustic/AcousticTask.h>

namespace Contract = ds::Api::Acoustic::L1;
using TestInterpreter = ds::AcousticInterpreter;

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

BOOST_AUTO_TEST_CASE(task_rejects_wrong_payloads_and_null_async_input) {
    ds::test::DeclaredInference declaration;
    ds::AcousticInference executive(declaration.spec());
    ds::AcousticTask task(executive);
    OtherInit args;
    OtherInput input;
    auto initialized = task.initialize(static_cast<const srt::TaskInitArgs &>(args));
    BOOST_REQUIRE(!initialized);
    BOOST_CHECK(initialized.error().code() == srt::Error::InvalidArgument);
    auto result = task.start(static_cast<const srt::TaskStartInput &>(input));
    BOOST_REQUIRE(!result);
    BOOST_CHECK(result.error().code() == srt::Error::InvalidArgument);
    bool called = false;
    auto async = task.startAsync(std::shared_ptr<const srt::TaskStartInput>(),
                                 [&called](auto) { called = true; });
    BOOST_REQUIRE(!async);
    BOOST_CHECK(async.error().code() == srt::Error::InvalidArgument);
    BOOST_CHECK(!called);
    BOOST_REQUIRE(task.waitForFinished());
}
