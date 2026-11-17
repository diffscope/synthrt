#include <utility>

#include <dsinfer/Api/Inferences/Variance/1/VarianceApiL1.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace Api = ds::Api::Variance::L1;

BOOST_AUTO_TEST_CASE(payloads_preserve_contract_identity_across_base_views) {
    const srt::ContribContractTriplet expected(Api::API_INTERFACE, Api::API_LEVEL,
                                               Api::API_VARIANT);
    Api::VarianceSchema schema;
    Api::VarianceConfiguration configuration;
    Api::VarianceImportOptions options;
    Api::VarianceRuntimeOptions runtime;
    const srt::ContribSpecPayload *specPayloads[] = {&schema, &configuration, &options, &runtime};
    for (const auto payload : specPayloads) {
        BOOST_CHECK(payload->triplet() == expected);
    }
    Api::VarianceInitArgs args;
    Api::VarianceStartInput input;
    Api::VarianceResult result;
    const srt::TaskPayload *taskPayloads[] = {&args, &input, &result};
    for (const auto payload : taskPayloads) {
        BOOST_CHECK_EQUAL(payload->type(), Api::API_INTERFACE);
        BOOST_CHECK_EQUAL(payload->version(), Api::API_LEVEL);
    }
    auto moved = std::move(input);
    BOOST_CHECK_EQUAL(moved.type(), Api::API_INTERFACE);
    BOOST_CHECK_EQUAL(moved.version(), Api::API_LEVEL);
}
