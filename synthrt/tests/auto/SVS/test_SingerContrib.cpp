#define BOOST_TEST_MAIN
#include "TestSupport.h"

BOOST_AUTO_TEST_CASE(test_singer_category_ignores_contract_export_fields) {
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
              R"({"interface":"com.example.svs.Acoustic","variant":"default","level":1})");

    // These fields belong to the DiffSinger singer exports. The singer category does not
    // interpret them, so a malformed role map is not rejected in DataOnly mode.
    writeText(root / "modules" / "singer.json",
              R"({"interface":"com.example.svs.Singer","variant":"d","level":1,
                  "languages":{"cmn":"lang/missing"},"defaultLanguage":"cmn",
                  "reservedPhonemes":["AP","AP"],
                  "exports":{},
                  "imports":[{"role":"lang/cmn","ref":":inference/acoustic"}]})");

    srt::SynthUnit unit;
    auto opened = unit.openPackage(root, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(opened);
    auto singer = opened->contribution("singer", "singer1")->as<srt::SingerSpec>();
    BOOST_CHECK(singer->manifestExports().toObject().empty());
}