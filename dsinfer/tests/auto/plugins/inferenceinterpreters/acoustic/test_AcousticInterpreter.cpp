#include <memory>
#include <string>

#include <acoustic/AcousticInterpreter.h>
#include <acoustic/AcousticInference.h>
#include <acoustic/AcousticTask.h>

namespace Contract = ds::Api::Acoustic::L1;
using TestInterpreter = ds::AcousticInterpreter;

#define BOOST_TEST_MAIN
#include "../TestSupport.h"

BOOST_AUTO_TEST_CASE(interpreter_reads_typed_declaration) {
    ds::test::DeclaredInference declaration;
    ds::AcousticInterpreter interpreter;
    auto &spec = declaration.spec();
    auto exports = interpreter.createExports(spec);
    BOOST_REQUIRE(exports);
    BOOST_CHECK(exports.value()->contract() == spec.triplet().contract());
    auto configuration = interpreter.createConfiguration(spec);
    BOOST_REQUIRE(configuration);
    BOOST_CHECK(configuration.value()->triplet() == spec.triplet());
    auto options =
        interpreter.createImportOptions(spec, srt::JsonObject{
                                                  {"predictions", srt::JsonArray{"energy"}}
    });
    BOOST_REQUIRE(options);
    BOOST_CHECK(options.value()->contract() == spec.triplet().contract());
    Contract::RuntimeOptions runtime;
    auto executive = interpreter.createInference(spec, *options.value(), runtime);
    BOOST_REQUIRE(executive);
    BOOST_CHECK(&executive.value()->spec() == &spec);
    BOOST_CHECK(executive.value()->state() == srt::ITask::Idle);
    BOOST_REQUIRE(executive.value()->waitForFinished());
}
