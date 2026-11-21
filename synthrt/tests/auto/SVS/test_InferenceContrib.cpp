#define BOOST_TEST_MAIN
#include "TestSupport.h"

BOOST_AUTO_TEST_CASE(test_runtime_options_carry_contract_identity) {
    TestRuntimeOptions options;

    BOOST_CHECK_EQUAL(options.contract().interfaceId(), "com.example.svs.Acoustic");
    BOOST_CHECK_EQUAL(options.contract().level(), 1);

    TestSingerPipelineRuntimeOptions singerOptions;
    BOOST_CHECK_EQUAL(singerOptions.contract().interfaceId(), "com.example.svs.Singer");
    BOOST_CHECK_EQUAL(singerOptions.contract().level(), 1);
}

BOOST_AUTO_TEST_CASE(test_builtin_categories_parse_typed_data_only_specs) {
    TemporaryDirectory temporary;
    const auto root = temporary.path();
    writeText(root / "desc.json",
              R"({
                  "$version":"1.0",
                  "id":"voice",
                  "version":"1",
                  "runtimeLevel":1,
                  "contributions":{
                    "inference":[{"id":"acoustic","path":"modules/inference.json"}],
                    "singer":[{"id":"singer1","path":"modules/singer.json"}]
                  }
              })");
    writeText(root / "modules" / "inference.json",
              R"({
                  "interface":"com.example.svs.Acoustic",
                  "variant":"default",
                  "level":1,
                  "aFieldThisRuntimeDoesNotKnow":true,
                  "exports":{},
                  "configuration":{}
              })");
    writeText(root / "modules" / "singer.json",
              R"({
                  "interface":"com.example.svs.Singer",
                  "variant":"diffsinger",
                  "level":1,
                  "avatar":{"_":"../assets/singer1/avatar.png","zh-CN":"../assets/singer1/avatar-zh.png"},
                  "background":"../assets/singer1/background.png",
                  "demoAudio":"../assets/singer1/demo.wav",
                  "aFieldThisRuntimeDoesNotKnow":true,
                  "exports":{
                    "languages":{"cmn":"lang/cmn","eng":"lang/eng"},
                    "defaultLanguage":"cmn",
                    "reservedPhonemes":["AP","SP"]
                  },
                  "configuration":{},
                  "imports":[
                    {"role":"lang/cmn","ref":":inference/acoustic"},
                    {"role":"lang/eng","ref":":inference/acoustic"}
                  ]
              })");

    srt::SynthUnit unit;
    BOOST_REQUIRE(unit.category("inference"));
    BOOST_REQUIRE(unit.category("singer"));
    auto opened = unit.openPackage(root, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(opened);
    auto package = opened.take();

    auto inferenceContribution = package.contribution("inference", "acoustic");
    BOOST_REQUIRE(inferenceContribution);
    auto inference = inferenceContribution->as<srt::InferenceSpec>();
    BOOST_REQUIRE(inference);
    BOOST_CHECK(inferenceContribution->declarationPath() == root / "modules" / "inference.json");
    BOOST_CHECK(inference->declarationPath() == root / "modules" / "inference.json");
    BOOST_CHECK_EQUAL(inference->triplet().interfaceId(), "com.example.svs.Acoustic");

    auto singerContribution = package.contribution("singer", "singer1");
    BOOST_REQUIRE(singerContribution);
    auto singer = singerContribution->as<srt::SingerSpec>();
    BOOST_REQUIRE(singer);
    BOOST_CHECK(singer->declarationPath() == root / "modules" / "singer.json");
    BOOST_CHECK_EQUAL(singer->avatar().text(),
                      (root / "assets" / "singer1" / "avatar.png").string());
    BOOST_CHECK_EQUAL(singer->avatar().text("zh-CN"),
                      (root / "assets" / "singer1" / "avatar-zh.png").string());
    // The singer category keeps exports as data. The DiffSinger provider interprets these fields.
    const auto &exports = singer->manifestExports().toObject();
    BOOST_REQUIRE_EQUAL(exports.at("languages").toObject().size(), 2u);
    BOOST_CHECK_EQUAL(exports.at("languages").toObject().at("cmn").toString(), "lang/cmn");
    BOOST_CHECK_EQUAL(exports.at("defaultLanguage").toString(), "cmn");
    BOOST_REQUIRE_EQUAL(exports.at("reservedPhonemes").toArray().size(), 2u);
    BOOST_CHECK_EQUAL(unit.category("inference")->interpreterIid(),
                      srt::InferenceInterpreterPlugin::IID);
    BOOST_CHECK_EQUAL(unit.category("singer")->interpreterIid(), srt::SingerProviderPlugin::IID);
}
