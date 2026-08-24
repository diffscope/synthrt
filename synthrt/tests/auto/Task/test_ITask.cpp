#include <memory>
#include <string>

#include <synthrt/Task/ITask.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    class TestInitArgs final : public srt::TaskInitArgs {
    public:
        TestInitArgs() : TaskInitArgs("com.example.TestInit", 1) {
        }
    };

    class TestInput final : public srt::TaskStartInput {
    public:
        explicit TestInput(int value) : TaskStartInput("com.example.TestInput", 2), value(value) {
        }

        int value;
    };

    class TestResult final : public srt::TaskResult {
    public:
        explicit TestResult(int value) : TaskResult("com.example.TestResult", 3), value(value) {
        }

        int value;
    };

    class TestTask final : public srt::ITask {
    public:
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override {
            m_state.store(Running);
            const auto value = input.as<TestInput>()->value;
            m_state.store(Succeeded);
            return std::unique_ptr<srt::TaskResult>(new TestResult(value * 2));
        }

        srt::Expected<void> stop() override {
            m_state.store(Canceled);
            return {};
        }

        srt::Expected<void> waitForFinished() override {
            return {};
        }
    };

}

BOOST_AUTO_TEST_SUITE(test_ITask)

BOOST_AUTO_TEST_CASE(test_payload_identity_and_synchronous_result_ownership) {
    TestTask task;
    TestInitArgs initArgs;
    BOOST_REQUIRE(task.initialize(initArgs));
    BOOST_CHECK(task.state() == srt::ITask::Idle);

    TestInput input(21);
    auto result = task.start(input);
    BOOST_REQUIRE(result);
    BOOST_CHECK(task.state() == srt::ITask::Succeeded);
    BOOST_CHECK_EQUAL(result->get()->type(), "com.example.TestResult");
    BOOST_CHECK_EQUAL(result->get()->version(), 3);
    BOOST_CHECK_EQUAL(result->get()->as<TestResult>()->value, 42);
}

BOOST_AUTO_TEST_CASE(test_default_async_execution_is_unsupported) {
    TestTask task;
    auto input = std::make_shared<TestInput>(1);
    auto result = task.startAsync(input, [](auto) {});

    BOOST_REQUIRE(!result);
    BOOST_CHECK(result.error().code() == srt::Error::FeatureNotSupported);
}

BOOST_AUTO_TEST_CASE(test_stop_changes_state_in_the_implementation) {
    TestTask task;
    BOOST_REQUIRE(task.stop());
    BOOST_CHECK(task.state() == srt::ITask::Canceled);
    BOOST_REQUIRE(task.waitForFinished());
}

BOOST_AUTO_TEST_SUITE_END()
