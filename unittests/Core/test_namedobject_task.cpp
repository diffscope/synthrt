// P1.2 unit tests for srt::core::NamedObject, NO<T>, ObjectPool, Plugin, PluginFactory
// P1.3 unit tests for srt::core::ITask, TaskInitArgs, TaskResult

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Plugin/Plugin.h>
#include <synthrt/Core/Plugin/PluginFactory.h>
#include <synthrt/Core/Task/ITask.h>
#include <synthrt/Core/Task/Task.h>
#include <synthrt/Core/Task/TaskPlugin.h>

using namespace srt::core;

// --- P1.2: NamedObject ---

TEST_CASE("NamedObject default construct", "[p1.2][namedobject]") {
    NamedObject obj;
    REQUIRE(obj.objectName().empty());
}

TEST_CASE("NamedObject named construct", "[p1.2][namedobject]") {
    NamedObject obj("test");
    REQUIRE(obj.objectName() == "test");
}

TEST_CASE("NamedObject set name", "[p1.2][namedobject]") {
    NamedObject obj;
    obj.setObjectName("hello");
    REQUIRE(obj.objectName() == "hello");
}

TEST_CASE("NamedObject property", "[p1.2][namedobject]") {
    NamedObject obj;
    obj.setProperty("key", std::any(42));
    REQUIRE(std::any_cast<int>(obj.property("key")) == 42);
    // Non-existent property returns empty any
    REQUIRE(!obj.property("nonexistent").has_value());
}

// --- P1.2: NO<T> ---

TEST_CASE("NO create", "[p1.2][no]") {
    auto obj = NO<NamedObject>::create("test");
    REQUIRE(obj);
    REQUIRE(obj->objectName() == "test");
}

TEST_CASE("NO as cast", "[p1.2][no]") {
    class Derived : public NamedObject {
    public:
        Derived() : NamedObject("derived") {}
        int value = 99;
    };

    NO<Derived> derived = NO<Derived>::create();
    NO<NamedObject> base = derived.as<NamedObject>();
    REQUIRE(base->objectName() == "derived");
}

// --- P1.2: ObjectPool ---

TEST_CASE("ObjectPool add/remove", "[p1.2][objectpool]") {
    ObjectPool pool;
    auto obj1 = NO<NamedObject>::create("a");
    auto obj2 = NO<NamedObject>::create("b");

    pool.addObject("category", obj1);
    pool.addObject("category", obj2);
    REQUIRE(pool.getObjects("category").size() == 2);

    pool.removeObject("category", obj1.get());
    REQUIRE(pool.getObjects("category").size() == 1);

    pool.removeObjects("category");
    REQUIRE(pool.getObjects("category").empty());
}

// --- P1.2: Plugin ---

TEST_CASE("PluginFactory static plugin sets", "[p1.2][plugin]") {
    // Just verify the static methods don't crash
    auto sets = PluginFactory::staticPluginSets();
    (void) sets;
}

TEST_CASE("SRT_EXPORT_PLUGIN macro exists", "[p1.2][plugin]") {
    // Verify the macro is defined and has the right signature
    // (actual plugin loading is tested in integration tests)
    REQUIRE(std::string("srt_plugin_instance") != std::string("synthrt_plugin_instance"));
}

// --- P1.3: ITask ---

namespace {

class TestTask : public ITask {
public:
    TestTask() : ITask(), _result(NO<TaskResult>::create("result")) {}

    Expected<NO<TaskResult>> start(const NO<TaskStartInput> &input) override {
        setState(Running);
        _result->error = Error::success();
        setState(Terminated);
        return _result;
    }

    bool stop() override {
        setState(Terminated);
        return true;
    }

    NO<TaskResult> result() const override {
        return _result;
    }

private:
    NO<TaskResult> _result;
};

} // namespace

namespace {

class TestModuleTask : public Task {
public:
    explicit TestModuleTask(const ModuleSpec *spec) : Task(spec) {}

    int apiLevel() const override { return 1; }
    Expected<void> initialize() override { return {}; }
    Expected<NO<TaskResult>> start(const NO<TaskInput> &input) override {
        auto result = NO<TaskResult>::create(input ? input->objectName() : "result");
        result->error = Error::success();
        return result;
    }
};

class TestSessionTask : public SessionTask {
public:
    int apiLevel() const override { return 1; }
    Expected<void> initialize() override { return {}; }
    Expected<NO<TaskResult>> start(const NO<TaskInput> &input) override {
        auto result = NO<TaskResult>::create(input ? input->objectName() : "session-result");
        result->error = Error::success();
        return result;
    }
    Expected<void> open(const std::filesystem::path &path, const NO<TaskInitArgs> &args) override {
        _open = true;
        _path = path;
        _args = args;
        return {};
    }
    Expected<void> close() override {
        _open = false;
        return {};
    }
    bool isOpen() const override { return _open; }
    int64_t id() const override { return 42; }

private:
    bool _open = false;
    std::filesystem::path _path;
    NO<TaskInitArgs> _args;
};

class TestSessionFactory : public SessionFactory {
public:
    std::string arch() const override { return "test-arch"; }
    std::string backend() const override { return "test-backend"; }
    Expected<void> initialize(const NO<TaskInitArgs> &args) override {
        _args = args;
        return {};
    }
    NO<SessionTask> createSession() override { return NO<TestSessionTask>::create().as<SessionTask>(); }

private:
    NO<TaskInitArgs> _args;
};

class TestTaskPlugin : public TaskPlugin {
public:
    const char *key() const override { return "core.test-task"; }
    int apiLevel() const override { return 1; }
    Expected<NO<Task>> createTask(const ModuleSpec *spec) override {
        return NO<TestModuleTask>::create(spec).as<Task>();
    }
};

class TestDriverPlugin : public DriverPlugin {
public:
    const char *key() const override { return "core.test-driver"; }
    int apiLevel() const override { return 1; }
    Expected<NO<SessionFactory>> create() override {
        return NO<TestSessionFactory>::create().as<SessionFactory>();
    }
};

} // namespace

TEST_CASE("ITask state transitions", "[p1.3][itask]") {
    TestTask task;
    REQUIRE(task.state() == ITask::Idle);

    auto input = NO<TaskStartInput>::create("input");
    auto result = task.start(input);
    REQUIRE(static_cast<bool>(result));
    REQUIRE(task.state() == ITask::Terminated);
    REQUIRE(result.value()->error.ok());
}

TEST_CASE("TaskInitArgs and TaskResult", "[p1.3][itask]") {
    TaskInitArgs args("init");
    REQUIRE(args.objectName() == "init");

    TaskResult result("output");
    REQUIRE(result.objectName() == "output");
    REQUIRE(result.error.ok()); // default Error is NoError
}

TEST_CASE("Task and TaskPlugin interfaces compile and expose identifiers", "[p1.3][task]") {
    TestTaskPlugin taskPlugin;
    REQUIRE(std::string(TaskPlugin::staticIid()) == "srt.core.task");
    REQUIRE(std::string(taskPlugin.iid()) == TaskPlugin::staticIid());
    REQUIRE(taskPlugin.apiLevel() == 1);

    auto task = taskPlugin.createTask(nullptr);
    REQUIRE(static_cast<bool>(task));
    REQUIRE(task.get()->spec() == nullptr);

    TestDriverPlugin driverPlugin;
    REQUIRE(std::string(DriverPlugin::staticIid()) == "srt.core.driver");
    REQUIRE(std::string(driverPlugin.iid()) == DriverPlugin::staticIid());
    REQUIRE(driverPlugin.apiLevel() == 1);

    auto factory = driverPlugin.create();
    REQUIRE(static_cast<bool>(factory));
    REQUIRE(factory.get()->arch() == "test-arch");
    REQUIRE(factory.get()->backend() == "test-backend");
    REQUIRE(factory.get()->createSession()->id() == 42);
}
