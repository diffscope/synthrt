#include <system_error>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <synthrt/Core/SynthUnit.h>

#include <inferutil/Parser.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    struct Declaration {
        std::filesystem::path root =
            std::filesystem::temp_directory_path() /
            ("parser-tests-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        srt::SynthUnit unit;
        srt::PackageHandle package;
        Declaration(const std::string &configuration, const std::string &exports = "{}") {
            std::filesystem::create_directories(root);
            write(
                "desc.json",
                R"({"$version":"1.0","id":"parser-tests","version":"1","runtimeLevel":1,"contributions":{"inference":[{"id":"test","path":"module.json"}]}})");
            write(
                "module.json",
                std::string(
                    R"({"interface":"com.example.Model","level":1,"variant":"test","configuration":)") +
                    configuration + ",\"exports\":" + exports + "}");
            auto opened = unit.openPackage(root, srt::SynthUnit::DataOnly);
            BOOST_REQUIRE(opened);
            package = opened.take();
        }
        ~Declaration() {
            package.reset();
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
        void write(const char *name, const std::string &text) {
            std::ofstream stream(root / name, std::ios::binary);
            stream << text;
            BOOST_REQUIRE(stream.good());
        }
        const srt::InferenceSpec *spec() const {
            return package.contribution("inference", "test")->as<srt::InferenceSpec>();
        }
    };

}

BOOST_AUTO_TEST_CASE(configuration_parser_keeps_defaults_and_resolves_paths_and_frame_width) {
    Declaration declaration(
        R"({"flag":true,"count":8,"positive":3,"ratio":0.25,"model":"model.onnx","sampleRate":100,"hopSize":2,"phonemes":"phones.json"})");
    declaration.write("phones.json", R"({"SP":0,"a":1})");
    ds::inferutil::ErrorCollector errors;
    ds::inferutil::ConfigurationParser parser(declaration.spec(), &errors);
    bool flag = false;
    int count = 1, positive = 1, missing = 17;
    double ratio = 0, frameWidth = 0;
    std::filesystem::path model;
    std::map<std::string, int> phones;
    parser.parse_bool_optional(flag, "flag");
    parser.parse_int_optional(count, "count");
    parser.parse_positive_int_optional(positive, "positive");
    parser.parse_int_optional(missing, "missing");
    parser.parse_double_optional(ratio, "ratio");
    parser.parse_path_required(model, "model");
    parser.parse_frameWidth(frameWidth);
    parser.parse_phonemes(phones);
    BOOST_CHECK(!errors.hasErrors());
    BOOST_CHECK(flag);
    BOOST_CHECK_EQUAL(count, 8);
    BOOST_CHECK_EQUAL(positive, 3);
    BOOST_CHECK_EQUAL(missing, 17);
    BOOST_CHECK_EQUAL(ratio, 0.25);
    BOOST_CHECK_CLOSE(frameWidth, 0.02, 0.0001);
    BOOST_CHECK(model == declaration.root / "model.onnx");
    BOOST_CHECK_EQUAL(phones.at("a"), 1);
}

BOOST_AUTO_TEST_CASE(parsers_collect_independent_failures_and_validate_schema_and_options) {
    Declaration declaration(R"({"flag":"bad","positive":0,"model":5})",
                            R"({"speakers":["main","other"],"predictions":["energy"]})");
    ds::inferutil::ErrorCollector errors;
    ds::inferutil::ConfigurationParser configuration(declaration.spec(), &errors);
    bool flag = false;
    int positive = 1;
    std::filesystem::path model;
    configuration.parse_bool_optional(flag, "flag");
    configuration.parse_positive_int_optional(positive, "positive");
    configuration.parse_path_required(model, "model");
    BOOST_CHECK_EQUAL(errors.errors().size(), 3u);
    errors.clear();
    ds::inferutil::SchemaParser schema(declaration.spec(), &errors);
    std::vector<std::string> speakers;
    std::vector<ds::ParamTag> predictions;
    schema.parse_string_array_optional(speakers, "speakers");
    schema.parse_parameters<ds::inferutil::ParameterType::Variance>(predictions, "predictions");
    BOOST_CHECK(!errors.hasErrors());
    BOOST_CHECK(speakers == std::vector<std::string>({"main", "other"}));
    BOOST_REQUIRE_EQUAL(predictions.size(), 1u);
    BOOST_CHECK(predictions[0] == ds::Api::Common::L1::Tags::Energy);
    const srt::JsonObject options{
        {"speakerMapping", srt::JsonObject{{"host", "model"}}},
        {"predictions",    srt::JsonArray{"energy"}          }
    };
    ds::inferutil::ImportOptionsParser parser(declaration.spec(), &errors, options);
    std::map<std::string, std::string> mapping;
    std::set<ds::ParamTag> selected;
    parser.parse_speakerMapping(mapping);
    parser.parse_parameters<ds::inferutil::ParameterType::Variance>(selected, "predictions");
    BOOST_CHECK(!errors.hasErrors());
    BOOST_CHECK_EQUAL(mapping.at("host"), "model");
    BOOST_CHECK_EQUAL(selected.count(ds::Api::Common::L1::Tags::Energy), 1u);
}
