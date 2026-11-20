#include <utility>

#include <dsinfer/Api/Inferences/Pitch/1/PitchApiL1.h>
#include <dsinfer/Api/Inferences/Pitch/1/PitchOnnxApiL1.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace Api = ds::Api::Pitch::L1;

BOOST_AUTO_TEST_CASE(payloads_preserve_contract_identity_across_base_views) {
    const srt::ContribContractTriplet expected(Api::Contract.interfaceId(), Api::Contract.level(),
                                               Api::Onnx::Triplet.variant());
    Api::Exports schema;
    Api::Onnx::Configuration configuration;
    BOOST_CHECK(configuration.triplet() == Api::Onnx::Triplet);
    Api::ImportOptions options;
    Api::RuntimeOptions runtime;
    const srt::ContribSpecPayload *specPayloads[] = {&schema, &configuration, &options, &runtime};
    for (const auto payload : specPayloads) {
        BOOST_CHECK(payload->contract() == expected.contract());
    }
    Api::InitArgs args;
    Api::StartInput input;
    Api::Result result;
    const srt::TaskPayload *taskPayloads[] = {&args, &input, &result};
    for (const auto payload : taskPayloads) {
        BOOST_CHECK(payload->contract() == Api::Contract);
    }
    auto moved = std::move(input);
    BOOST_CHECK(moved.contract() == Api::Contract);
}
