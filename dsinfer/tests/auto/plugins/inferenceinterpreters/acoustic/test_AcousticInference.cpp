#include <memory>
#include <string>

#include <acoustic/AcousticInterpreter.h>
#include <acoustic/AcousticInference.h>
#include <acoustic/AcousticTask.h>

namespace Contract = ds::Api::Acoustic::L1;
using TestInterpreter = ds::AcousticInterpreter;

#define BOOST_TEST_MAIN
#include "../TestSupport.h"

BOOST_AUTO_TEST_CASE(executive_reports_missing_backend_and_can_be_stopped) {
    ds::test::DeclaredInference declaration;
    ds::AcousticInference executive(declaration.spec());
    BOOST_CHECK(&executive.spec() == &declaration.spec());
    BOOST_CHECK(executive.state() == srt::ITask::Idle);
    Contract::AcousticStartInput input;
    auto beforeInitialization = executive.start(input);
    BOOST_REQUIRE(!beforeInitialization);
    Contract::AcousticInitArgs args;
    auto initialized = executive.initialize(args);
    BOOST_REQUIRE(!initialized);
    BOOST_CHECK(executive.state() == srt::ITask::Failed);
    BOOST_REQUIRE(executive.stop());
    BOOST_REQUIRE(executive.waitForFinished());
}
