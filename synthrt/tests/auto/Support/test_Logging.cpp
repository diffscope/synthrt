#include <string>
#include <type_traits>
#include <vector>

#include <stdcorelib/support/logging.h>

#include <synthrt/Support/Logging.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    /// Collects what the sink is handed, so a test can say what was logged rather than look at a
    /// console.
    struct Record {
        int level;
        std::string category;
        std::string message;
    };

    std::vector<Record> records;

    void collect(int level, const stdc::LogContext &context, const std::string_view &message) {
        records.push_back({level, context.category ? context.category : "", std::string(message)});
    }

    /// Puts the sink back on the way out, so one case cannot take the others down with it.
    struct Sink {
        Sink() {
            records.clear();
            stdc::Logger::setLogCallback(&collect);
        }
        ~Sink() {
            stdc::Logger::setLogCallback(nullptr);
        }
    };

}

BOOST_AUTO_TEST_SUITE(test_Logging)

// The names are aliases, not types of their own, which is what lets the two spellings be mixed and
// share one registry.
BOOST_AUTO_TEST_CASE(test_Logging_AliasesAreTheSameTypes) {
    static_assert(std::is_same_v<srt::LogContext, stdc::LogContext>);
    static_assert(std::is_same_v<srt::Logger, stdc::Logger>);
    static_assert(std::is_same_v<srt::LogCategory, stdc::LogCategory>);

    // Levels come from the one enumeration, so a number means the same thing either way.
    BOOST_CHECK(srt::Logger::Warning == stdc::Logger::Warning);
    BOOST_CHECK(srt::Logger::Fatal > srt::Logger::Trace);
}

BOOST_AUTO_TEST_CASE(test_Logging_SynthRTCategory) {
    auto &first = srt::logCategory();
    auto &second = srt::logCategory();

    BOOST_CHECK_EQUAL(first.name(), "synthrt");
    BOOST_CHECK(&first == &second);
}

BOOST_AUTO_TEST_CASE(test_Logging_MacrosForward) {
    Sink sink;

    srtWarning("plain");
    srtInfo("with an argument: %1", 42);
    srtWarningF("formatted: %d", 7);

    BOOST_REQUIRE(records.size() == 3);
    BOOST_CHECK(records[0].level == stdc::Logger::Warning);
    BOOST_CHECK(records[0].message == "plain");
    BOOST_CHECK(records[1].level == stdc::Logger::Information);
    BOOST_CHECK(records[1].message == "with an argument: 42");
    BOOST_CHECK(records[2].level == stdc::Logger::Warning);
    BOOST_CHECK(records[2].message == "formatted: 7");
}

// A category written with the srt spelling reaches the same sink under its own name.
BOOST_AUTO_TEST_CASE(test_Logging_Category) {
    Sink sink;

    srt::LogCategory category("test-category");
    category.setLevelEnabled(srt::Logger::Debug, true);
    category.srtDebug("from a category");

    BOOST_REQUIRE(records.size() == 1);
    BOOST_CHECK(records[0].category == "test-category");
    BOOST_CHECK(records[0].message == "from a category");
}

// A level the category has switched off produces nothing at all.
BOOST_AUTO_TEST_CASE(test_Logging_LevelFilter) {
    Sink sink;

    srt::LogCategory category("filtered");
    category.setLevelEnabled(srt::Logger::Debug, false);
    category.setLevelEnabled(srt::Logger::Warning, true);

    BOOST_CHECK(!category.isLevelEnabled(srt::Logger::Debug));
    BOOST_CHECK(category.isLevelEnabled(srt::Logger::Warning));

    category.srtDebug("dropped");
    BOOST_CHECK(records.empty());

    category.srtWarning("kept");
    BOOST_REQUIRE(records.size() == 1);
    BOOST_CHECK(records[0].message == "kept");
}

BOOST_AUTO_TEST_SUITE_END()
