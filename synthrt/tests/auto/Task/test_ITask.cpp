#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>

#include <synthrt/Task/ITask.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    class TestInitArgs final : public srt::TaskInitArgs {
    public:
        TestInitArgs() : TaskInitArgs({"com.example.TestInit", 1}) {
        }
    };

    class TestInput final : public srt::TaskStartInput {
    public:
        explicit TestInput(int value) : TaskStartInput({"com.example.TestInput", 2}), value(value) {
        }

        int value;
    };

    class TestResult final : public srt::TaskResult {
    public:
        explicit TestResult(int value) : TaskResult({"com.example.TestResult", 3}), value(value) {
        }

        int value;
    };

    class TestTask final : public srt::ITask {
    public:
    protected:
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            execute(const srt::TaskStartInput &input, const srt::TaskStopToken &) override {
            const auto value = input.as<TestInput>()->value;
            return std::unique_ptr<srt::TaskResult>(new TestResult(value * 2));
        }
    };

    class BlockingTask final : public srt::ITask {
    public:
        ~BlockingTask() {
            (void) stop();
            (void) waitForFinished();
        }

    protected:
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            execute(const srt::TaskStartInput &input, const srt::TaskStopToken &token) override {
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_started = true;
                m_changed.notify_all();
                m_changed.wait(lock, [&token] { return token.stopRequested(); });
                if (token.stopRequested()) {
                    return srt::Error(srt::Error::Canceled, "test execution was canceled");
                }
            }
            return std::unique_ptr<srt::TaskResult>(new TestResult(input.as<TestInput>()->value));
        }

        srt::Expected<void> quit() override {
            m_changed.notify_all();
            return {};
        }

    public:
        void waitUntilStarted() {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_changed.wait(lock, [this] { return m_started; });
        }

    private:
        std::mutex m_mutex;
        std::condition_variable m_changed;
        bool m_started = false;
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
    BOOST_CHECK(result->get()->contract() == srt::ContribContract("com.example.TestResult", 3));
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

    auto overlappingSync = task.start(TestInput(3));
    BOOST_REQUIRE(!overlappingSync);
    BOOST_CHECK(overlappingSync.error().code() == srt::Error::InvalidArgument);

    BOOST_REQUIRE(task.stop());
    BOOST_REQUIRE(task.waitForFinished());
    auto callbackResult = callbackFuture.get();
    BOOST_CHECK(!callbackResult);
    BOOST_CHECK(task.state() == srt::ITask::Canceled);
}

BOOST_AUTO_TEST_CASE(test_callback_may_start_the_next_execution) {
    TestTask task;
    std::promise<void> completed;
    auto future = completed.get_future();
    std::atomic<int> callbacks = 0;
    std::atomic<bool> firstSucceeded = false;
    std::atomic<bool> secondStarted = false;
    std::atomic<bool> secondSucceeded = false;

    auto input = std::make_shared<TestInput>(4);
    BOOST_REQUIRE(task.startAsync(input, [&](auto first) mutable {
        firstSucceeded = static_cast<bool>(first);
        ++callbacks;
        auto second = task.startAsync(input, [&](auto result) mutable {
            secondSucceeded = static_cast<bool>(result);
            ++callbacks;
            completed.set_value();
        });
        secondStarted = static_cast<bool>(second);
        if (!second) {
            completed.set_value();
        }
    }));

    BOOST_REQUIRE(future.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    BOOST_REQUIRE(task.waitForFinished());
    BOOST_REQUIRE(firstSucceeded.load());
    BOOST_REQUIRE(secondStarted.load());
    BOOST_REQUIRE(secondSucceeded.load());
    BOOST_CHECK_EQUAL(callbacks.load(), 2);
    BOOST_CHECK(task.state() == srt::ITask::Succeeded);
}

BOOST_AUTO_TEST_CASE(test_task_handle_controls_one_asynchronous_execution) {
    BlockingTask task;
    std::promise<srt::Expected<std::unique_ptr<srt::TaskResult>>> promise;
    auto future = promise.get_future();

    auto started = task.startAsync(std::make_shared<TestInput>(1), [&promise](auto result) mutable {
        promise.set_value(std::move(result));
    });
    BOOST_REQUIRE(started);
    auto handle = started.take();
    BOOST_CHECK(handle.valid());
    BOOST_CHECK(handle.isActive());
    task.waitUntilStarted();

    BOOST_REQUIRE(handle.stop());
    BOOST_REQUIRE(handle.wait());
    BOOST_CHECK(handle.isFinished());
    auto result = future.get();
    BOOST_REQUIRE(!result);
    BOOST_CHECK(result.error().code() == srt::Error::Canceled);
    BOOST_CHECK(task.state() == srt::ITask::Canceled);
}

BOOST_AUTO_TEST_CASE(test_task_stop_token_is_shared_by_copies) {
    srt::TaskStopSource source;
    auto first = source.token();
    auto second = first;
    BOOST_CHECK(!first.stopRequested());
    BOOST_CHECK(!second.stopRequested());

    source.requestStop();
    BOOST_CHECK(first.stopRequested());
    BOOST_CHECK(second.stopRequested());
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

BOOST_AUTO_TEST_CASE(test_task_handle_remains_safe_after_task_destruction) {
    auto task = std::make_unique<TestTask>();
    std::promise<void> callbackEntered;
    auto callbackEnteredFuture = callbackEntered.get_future();
    std::promise<void> releaseCallback;
    auto releaseCallbackFuture = releaseCallback.get_future().share();

    auto started = task->startAsync(std::make_shared<TestInput>(3),
                                    [&task, &callbackEntered, releaseCallbackFuture](auto) mutable {
                                        task.reset();
                                        callbackEntered.set_value();
                                        releaseCallbackFuture.wait();
                                    });
    BOOST_REQUIRE(started);
    auto handle = started.take();
    callbackEnteredFuture.get();
    BOOST_CHECK(!task);

    BOOST_REQUIRE(handle.stop());
    auto waiter = std::async(std::launch::async, [&handle] { return handle.wait(); });
    BOOST_CHECK(waiter.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    releaseCallback.set_value();
    BOOST_REQUIRE(waiter.get());
    BOOST_CHECK(handle.isFinished());
}

BOOST_AUTO_TEST_CASE(test_callback_exception_releases_the_execution) {
    TestTask task;
    std::promise<void> delivered;
    auto deliveredFuture = delivered.get_future();

    BOOST_REQUIRE(task.startAsync(std::make_shared<TestInput>(5), [&delivered](auto) mutable {
        delivered.set_value();
        throw std::runtime_error("callback failed");
    }));
    deliveredFuture.get();
    BOOST_REQUIRE(task.waitForFinished());

    TestInput input(6);
    auto next = task.start(input);
    BOOST_REQUIRE(next);
    BOOST_CHECK(task.state() == srt::ITask::Succeeded);
}

BOOST_AUTO_TEST_CASE(test_body_exception_is_internal_and_the_task_remains_reusable) {
    class ThrowingTask final : public srt::ITask {
    public:
        bool fail = true;

    protected:
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            execute(const srt::TaskStartInput &input, const srt::TaskStopToken &) override {
            if (fail) {
                throw std::runtime_error("body failed");
            }
            return std::unique_ptr<srt::TaskResult>(new TestResult(input.as<TestInput>()->value));
        }
    };

    ThrowingTask task;
    TestInput input(7);
    auto failed = task.start(input);
    BOOST_REQUIRE(!failed);
    BOOST_CHECK(failed.error().code() == srt::Error::Internal);
    BOOST_CHECK(task.state() == srt::ITask::Failed);

    task.fail = false;
    auto succeeded = task.start(input);
    BOOST_CHECK(static_cast<bool>(succeeded));
    BOOST_CHECK(task.state() == srt::ITask::Succeeded);
}

BOOST_AUTO_TEST_CASE(test_stop_while_idle_does_not_change_state) {
    TestTask task;
    BOOST_REQUIRE(task.stop());
    BOOST_CHECK(task.state() == srt::ITask::Idle);
    BOOST_REQUIRE(task.waitForFinished());
}

BOOST_AUTO_TEST_SUITE_END()
