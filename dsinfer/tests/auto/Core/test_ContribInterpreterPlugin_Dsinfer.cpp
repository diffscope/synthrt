#include <filesystem>

#include <stdcorelib/plugin/pluginloader.h>

#include <synthrt/Core/ContribInterpreterPlugin.h>
#include <synthrt/SVS/InferenceInterpreterPlugin.h>
#include <synthrt/SVS/SingerProviderPlugin.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    void checkPlugin(const std::filesystem::path &file, const std::filesystem::path &metadata,
                     const char *iid) {
        stdc::plugin::PluginLoader loader(file, metadata);
        BOOST_REQUIRE(loader.state() == stdc::plugin::PluginLoader::Read);
        auto declarations =
            srt::ContribInterpreterPlugin::readInterpreterDeclarations(loader.metadata());
        BOOST_REQUIRE(declarations);
        BOOST_CHECK(!declarations.value().empty());
        BOOST_REQUIRE_MESSAGE(loader.load(), loader.errorMessage());
        BOOST_CHECK_EQUAL(loader.iid(), iid);
        auto plugin = static_cast<srt::ContribInterpreterPlugin *>(loader.plugin());
        BOOST_REQUIRE(plugin);
        for (const auto &triplet : declarations.value()) {
            auto created = plugin->create(triplet);
            BOOST_REQUIRE(created);
            BOOST_REQUIRE(created.value());
            auto otherLevel =
                plugin->create({triplet.interfaceId(), triplet.level() + 1, triplet.variant()});
            BOOST_REQUIRE(!otherLevel);
            auto otherVariant =
                plugin->create({triplet.interfaceId(), triplet.level(), "com.example.unsupported"});
            BOOST_REQUIRE(!otherVariant);
            auto otherInterface =
                plugin->create({"com.example.Unsupported", triplet.level(), triplet.variant()});
            BOOST_REQUIRE(!otherInterface);
        }
    }

}

BOOST_AUTO_TEST_CASE(acousticFactoryMatchesItsManifest) {
    checkPlugin(DSINFER_TEST_ACOUSTIC_FILE, DSINFER_TEST_ACOUSTIC_MANIFEST,
                srt::InferenceInterpreterPlugin::IID);
}

BOOST_AUTO_TEST_CASE(durationFactoryMatchesItsManifest) {
    checkPlugin(DSINFER_TEST_DURATION_FILE, DSINFER_TEST_DURATION_MANIFEST,
                srt::InferenceInterpreterPlugin::IID);
}

BOOST_AUTO_TEST_CASE(pitchFactoryMatchesItsManifest) {
    checkPlugin(DSINFER_TEST_PITCH_FILE, DSINFER_TEST_PITCH_MANIFEST,
                srt::InferenceInterpreterPlugin::IID);
}

BOOST_AUTO_TEST_CASE(varianceFactoryMatchesItsManifest) {
    checkPlugin(DSINFER_TEST_VARIANCE_FILE, DSINFER_TEST_VARIANCE_MANIFEST,
                srt::InferenceInterpreterPlugin::IID);
}

BOOST_AUTO_TEST_CASE(vocoderFactoryMatchesItsManifest) {
    checkPlugin(DSINFER_TEST_VOCODER_FILE, DSINFER_TEST_VOCODER_MANIFEST,
                srt::InferenceInterpreterPlugin::IID);
}

BOOST_AUTO_TEST_CASE(diffsingerFactoryMatchesItsManifest) {
    checkPlugin(DSINFER_TEST_DIFFSINGER_FILE, DSINFER_TEST_DIFFSINGER_MANIFEST,
                srt::SingerProviderPlugin::IID);
}
