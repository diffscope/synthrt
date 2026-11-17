#include <utility>

#include <dsinfer/Api/Inferences/Duration/1/DurationApiL1.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace Api = ds::Api::Duration::L1;

BOOST_AUTO_TEST_CASE(payloads_preserve_contract_identity_across_base_views) {
    const srt::ContribContractTriplet expected(Api::API_INTERFACE, Api::API_LEVEL,
                                               Api::API_VARIANT);
    Api::DurationSchema schema;
    Api::DurationConfiguration configuration;
    Api::DurationImportOptions options;
    Api::DurationRuntimeOptions runtime;
    const srt::ContribSpecPayload *specPayloads[] = {&schema, &configuration, &options, &runtime};
    for (const auto payload : specPayloads) {
        BOOST_CHECK(payload->triplet() == expected);
    }
    Api::DurationInitArgs args;
    Api::DurationStartInput input;
    Api::DurationResult result;
    const srt::TaskPayload *taskPayloads[] = {&args, &input, &result};
    for (const auto payload : taskPayloads) {
        BOOST_CHECK_EQUAL(payload->type(), Api::API_INTERFACE);
        BOOST_CHECK_EQUAL(payload->version(), Api::API_LEVEL);
    }
    auto moved = std::move(input);
    BOOST_CHECK_EQUAL(moved.type(), Api::API_INTERFACE);
    BOOST_CHECK_EQUAL(moved.version(), Api::API_LEVEL);
}
