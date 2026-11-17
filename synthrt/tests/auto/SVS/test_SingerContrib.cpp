#define BOOST_TEST_MAIN
#include "TestSupport.h"

BOOST_AUTO_TEST_CASE(test_singer_language_map_must_name_existing_roles) {
    TemporaryDirectory temporary;
    const auto root = temporary.path();
    const auto write = [&](const std::string &singer) {
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
        writeText(root / "modules" / "singer.json", singer);
    };

    srt::SynthUnit unit;
    write(R"({"interface":"com.example.svs.Singer","variant":"d","level":1,
              "languages":{"cmn":"lang/missing"},"defaultLanguage":"cmn",
              "imports":[{"role":"lang/cmn","ref":":inference/acoustic"}]})");
    BOOST_CHECK(!unit.openPackage(root, srt::SynthUnit::DataOnly));

    write(R"({"interface":"com.example.svs.Singer","variant":"d","level":1,
              "languages":{"cmn":"lang/cmn"},"defaultLanguage":"eng",
              "imports":[{"role":"lang/cmn","ref":":inference/acoustic"}]})");
    BOOST_CHECK(!unit.openPackage(root, srt::SynthUnit::DataOnly));

    write(R"({"interface":"com.example.svs.Singer","variant":"d","level":1,
              "languages":{"cmn":"lang/cmn"},
              "imports":[{"role":"lang/cmn","ref":":inference/acoustic"}]})");
    BOOST_CHECK(!unit.openPackage(root, srt::SynthUnit::DataOnly));

    write(R"({"interface":"com.example.svs.Singer","variant":"d","level":1,
              "reservedPhonemes":["AP","AP"]})");
    BOOST_CHECK(!unit.openPackage(root, srt::SynthUnit::DataOnly));

    write(R"({"interface":"com.example.svs.Singer","variant":"d","level":1,
              "reservedPhonemes":["AP",""]})");
    BOOST_CHECK(!unit.openPackage(root, srt::SynthUnit::DataOnly));

    write(R"({"interface":"com.example.svs.Singer","variant":"d","level":1})");
    auto plain = unit.openPackage(root, srt::SynthUnit::DataOnly);
    BOOST_REQUIRE(plain);
    auto singer = plain->contribution("singer", "singer1")->as<srt::SingerSpec>();
    BOOST_CHECK(singer->languages().empty());
    BOOST_CHECK(singer->defaultLanguage().empty());
    BOOST_CHECK(singer->reservedPhonemes().empty());
}
