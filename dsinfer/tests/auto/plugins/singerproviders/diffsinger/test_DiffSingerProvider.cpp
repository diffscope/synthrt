#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

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
    BOOST_CHECK(configuration.value()->as<Api::DiffSingerConfiguration>()->dict ==
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
    Api::DiffSingerPipelineRuntimeOptions options;
    auto pipeline = extension->createPipeline(options);
    BOOST_REQUIRE(pipeline);
    BOOST_CHECK(&pipeline.value()->spec() == singer);
}
