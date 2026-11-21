#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>
#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerOpenVpiApiL1.h>

#include <diffsinger/DiffSingerProvider.h>
#include <diffsinger/DiffSingerPipelineExecutive.h>

#define BOOST_TEST_MAIN
#include "TestSupport.h"

namespace Api = ds::Api::DiffSinger::L1;

BOOST_AUTO_TEST_CASE(provider_creates_typed_configuration_validators_and_pipeline_extension) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 44100);
    auto unit = makeUnit();
    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);
    BOOST_REQUIRE(opened);
    auto package = opened.take();
    auto singer = package.contribution("singer", "singer");
    BOOST_REQUIRE(singer);
    ds::DiffSingerProvider provider;
    auto configuration = provider.createConfiguration(*singer);
    BOOST_REQUIRE(configuration);
    BOOST_CHECK(configuration.value()->triplet() == singer->triplet());
    BOOST_CHECK(configuration.value()->as<Api::OpenVpi::Configuration>()->dict ==
                temporary.path() / "dictionary.txt");
    auto validators = provider.createImportValidators();
    BOOST_REQUIRE(validators);
    BOOST_REQUIRE_EQUAL(validators.value().size(), 1u);
    BOOST_REQUIRE(validators.value()[0]->validateImports(*singer));
    auto extensions = provider.createExtensions(*singer);
    BOOST_REQUIRE(extensions);
    BOOST_REQUIRE_EQUAL(extensions.value().size(), 1u);
    BOOST_CHECK(&extensions.value()[0]->spec() == singer);
    auto extension = extensions.value()[0]->as<srt::SingerPipelineExtension>();
    Api::PipelineRuntimeOptions options;
    auto pipeline = extension->createPipeline(options);
    BOOST_REQUIRE(pipeline);
    BOOST_CHECK(&pipeline.value()->spec() == singer);
}

BOOST_AUTO_TEST_CASE(provider_parses_singer_exports) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 44100, true, true, R"(["SP"])",
                 R"({"cmn":"singer/acoustic"})", "cmn");
    auto unit = makeUnit();
    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);
    BOOST_REQUIRE(opened);
    auto singer = opened->contribution("singer", "singer");
    BOOST_REQUIRE(singer);
    const auto *exports = singer->exports()->as<Api::Exports>();
    BOOST_CHECK_EQUAL(exports->languages.at("cmn"), "singer/acoustic");
    BOOST_CHECK_EQUAL(exports->defaultLanguage, "cmn");
    BOOST_REQUIRE_EQUAL(exports->reservedPhonemes.size(), 1u);
    BOOST_CHECK_EQUAL(exports->reservedPhonemes[0], "SP");
}

BOOST_AUTO_TEST_CASE(singer_exports_with_languages_require_a_default_language) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 44100, true, true, "", R"({"cmn":"singer/acoustic"})");
    auto unit = makeUnit();
    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);
    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().toString().find("defaultLanguage") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(singer_language_roles_must_exist) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 44100, true, true, "", R"({"cmn":"singer/missing"})",
                 "cmn");
    auto unit = makeUnit();
    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);
    BOOST_REQUIRE(!opened);
    BOOST_CHECK(opened.error().toString().find("nonexistent import role") != std::string::npos);
}