#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>

#include <diffsinger/DiffSingerProvider.h>
#include <diffsinger/DiffSingerPipelineExecutive.h>

#define BOOST_TEST_MAIN
#include "TestSupport.h"

namespace Api = ds::Api::DiffSinger::L1;

BOOST_AUTO_TEST_CASE(pipeline_supervises_imported_executives_and_reports_absent_optional_roles) {
    TemporaryDirectory temporary;
    writePackage(temporary.path(), 44100, 44100);
    auto unit = makeUnit();
    auto opened = unit.openPackage(temporary.path(), srt::SynthUnit::Load);
    BOOST_REQUIRE(opened);
    auto package = opened.take();
    auto singer = package.contribution("singer", "singer");
    BOOST_REQUIRE(singer);
    ds::DiffSingerPipelineExecutive pipeline(*singer->as<srt::SingerSpec>());
    auto acoustic = pipeline.createAcoustic(ds::Api::Acoustic::L1::AcousticRuntimeOptions());
    auto vocoder = pipeline.createVocoder(ds::Api::Vocoder::L1::VocoderRuntimeOptions());
    BOOST_REQUIRE(acoustic);
    BOOST_REQUIRE(vocoder);
    BOOST_CHECK(acoustic.value()->parent() == &pipeline);
    BOOST_CHECK(vocoder.value()->parent() == &pipeline);
    BOOST_CHECK_EQUAL(pipeline.children().size(), 2u);
    BOOST_CHECK(!pipeline.createDuration(ds::Api::Duration::L1::DurationRuntimeOptions()));
    BOOST_CHECK(!pipeline.createPitch(ds::Api::Pitch::L1::PitchRuntimeOptions()));
    BOOST_CHECK(!pipeline.createVariance(ds::Api::Variance::L1::VarianceRuntimeOptions()));
    delete acoustic.value();
    BOOST_CHECK_EQUAL(pipeline.children().size(), 1u);
}
