#include <memory>
#include <string>

#include <vocoder/VocoderInterpreter.h>
#include <vocoder/VocoderInference.h>
#include <vocoder/VocoderTask.h>

namespace Contract = ds::Api::Vocoder::L1;
using TestInterpreter = ds::VocoderInterpreter;

#define BOOST_TEST_MAIN
#include "../TestSupport.h"

BOOST_AUTO_TEST_CASE(executive_reports_missing_backend_and_can_be_stopped) {
    ds::test::DeclaredInference declaration;
    ds::VocoderInference executive(declaration.spec());
    BOOST_CHECK(&executive.spec() == &declaration.spec());
    BOOST_CHECK(executive.state() == srt::ITask::Idle);
    Contract::VocoderStartInput input;
    auto beforeInitialization = executive.start(input);
    BOOST_REQUIRE(!beforeInitialization);
    Contract::VocoderInitArgs args;
    auto initialized = executive.initialize(args);
    BOOST_REQUIRE(!initialized);
    BOOST_CHECK(executive.state() == srt::ITask::Failed);
    BOOST_REQUIRE(executive.stop());
    BOOST_REQUIRE(executive.waitForFinished());
}
