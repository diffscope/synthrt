#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>

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
            requestAsyncCancellation();
            m_state.store(Canceled);
            return {};
        }

        srt::Expected<void> waitForFinished() override {
            waitForAsyncExecution();
            return {};
        }
    };

    class BlockingTask final : public srt::ITask {
    public:
        ~BlockingTask() {
            if (state() == Running) {
                std::ignore = stop();
            }
            std::ignore = waitForFinished();
        }

        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override {
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                setState(Running);
                m_started = true;
                m_changed.notify_all();
                m_changed.wait(lock, [this] { return m_finishRequested || m_stopRequested; });
                if (m_stopRequested) {
                    return srt::Error(srt::Error::FeatureNotSupported,
                                      "test execution was canceled");
                }
            }
            setState(Succeeded);
            return std::unique_ptr<srt::TaskResult>(new TestResult(input.as<TestInput>()->value));
        }

        srt::Expected<void> stop() override {
            requestAsyncCancellation();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stopRequested = true;
            }
            setState(Canceled);
            m_changed.notify_all();
            return {};
        }

        srt::Expected<void> waitForFinished() override {
            waitForAsyncExecution();
            return {};
        }

        void waitUntilStarted() {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_changed.wait(lock, [this] { return m_started; });
        }

        void finish() {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_finishRequested = true;
            }
            m_changed.notify_all();
        }

    private:
        std::mutex m_mutex;
        std::condition_variable m_changed;
        bool m_started = false;
        bool m_finishRequested = false;
        bool m_stopRequested = false;
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

BOOST_AUTO_TEST_CASE(test_default_async_execution_delivers_result) {
    TestTask task;
    std::promise<srt::Expected<std::unique_ptr<srt::TaskResult>>> promise;
    auto future = promise.get_future();

    auto started =
        task.startAsync(std::make_shared<TestInput>(21),
                        [&promise](auto result) mutable { promise.set_value(std::move(result)); });

    BOOST_REQUIRE(started);
    auto result = future.get();
    BOOST_REQUIRE(task.waitForFinished());
    BOOST_REQUIRE(result);
    BOOST_CHECK_EQUAL(result->get()->as<TestResult>()->value, 42);
    BOOST_CHECK(task.state() == srt::ITask::Succeeded);
}

BOOST_AUTO_TEST_CASE(test_default_async_execution_rejects_invalid_or_overlapping_calls) {
    BlockingTask task;
    auto missingInput = task.startAsync({}, [](auto) {});
    BOOST_REQUIRE(!missingInput);
    BOOST_CHECK(missingInput.error().code() == srt::Error::InvalidArgument);

    auto missingCallback = task.startAsync(std::make_shared<TestInput>(1), {});
    BOOST_REQUIRE(!missingCallback);
    BOOST_CHECK(missingCallback.error().code() == srt::Error::InvalidArgument);

    std::promise<srt::Expected<std::unique_ptr<srt::TaskResult>>> callbackPromise;
    auto callbackFuture = callbackPromise.get_future();
    BOOST_REQUIRE(
        task.startAsync(std::make_shared<TestInput>(1), [&callbackPromise](auto result) mutable {
            callbackPromise.set_value(std::move(result));
        }));
    task.waitUntilStarted();
    auto overlapping = task.startAsync(std::make_shared<TestInput>(2), [](auto) {});
    BOOST_REQUIRE(!overlapping);
    BOOST_CHECK(overlapping.error().code() == srt::Error::InvalidArgument);

    BOOST_REQUIRE(task.stop());
    BOOST_REQUIRE(task.waitForFinished());
    auto callbackResult = callbackFuture.get();
    BOOST_CHECK(!callbackResult);
    BOOST_CHECK(task.state() == srt::ITask::Canceled);
}

BOOST_AUTO_TEST_CASE(test_wait_includes_callback_execution) {
    TestTask task;
    std::promise<void> callbackEntered;
    auto callbackEnteredFuture = callbackEntered.get_future();
    std::promise<void> releaseCallback;
    auto releaseCallbackFuture = releaseCallback.get_future().share();

    BOOST_REQUIRE(task.startAsync(std::make_shared<TestInput>(1),
                                  [&callbackEntered, releaseCallbackFuture](auto) mutable {
                                      callbackEntered.set_value();
                                      releaseCallbackFuture.wait();
                                  }));
    callbackEnteredFuture.get();

    auto waiter = std::async(std::launch::async, [&task] { return task.waitForFinished(); });
    BOOST_CHECK(waiter.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    releaseCallback.set_value();
    BOOST_REQUIRE(waiter.get());
}

BOOST_AUTO_TEST_CASE(test_callback_may_destroy_task) {
    auto task = std::make_unique<TestTask>();
    std::promise<srt::Expected<std::unique_ptr<srt::TaskResult>>> promise;
    auto future = promise.get_future();

    BOOST_REQUIRE(
        task->startAsync(std::make_shared<TestInput>(3), [&task, &promise](auto result) mutable {
            task.reset();
            promise.set_value(std::move(result));
        }));

    auto result = future.get();
    BOOST_REQUIRE(result);
    BOOST_CHECK(!task);
}

BOOST_AUTO_TEST_CASE(test_stop_changes_state_in_the_implementation) {
    TestTask task;
    BOOST_REQUIRE(task.stop());
    BOOST_CHECK(task.state() == srt::ITask::Canceled);
    BOOST_REQUIRE(task.waitForFinished());
}

BOOST_AUTO_TEST_SUITE_END()
