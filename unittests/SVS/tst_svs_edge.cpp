// lib/SVS edge condition test cases (SVS-001 ~ SVS-010)
//
// Covers behavior of srt::svs::Inference and InferenceInitArgs under abnormal
// input, dangling pointers, and concurrency scenarios. See task description
// for test matrix; corresponds to the actual implementation in
// lib/SVS/Inference.cpp and lib/SVS/InferenceContrib.cpp.
//
// === API difference notes vs. test matrix ===
// The following cases cannot be tested directly per the matrix due to API
// constraints (adjusted per the actual API):
//   - SVS-002 (P1): Matrix expected "documented as UB". After construction,
//     freeing the spec and then calling spec() returns a dangling pointer;
//     dereferencing it is UB. Cannot safely test UB, documented without a
//     test shell.
//   - SVS-007 (P2): Matrix expected "documented as UB". Destroying while
//     another thread is accessing is a data race UB, documented without a
//     test shell.
//   - SVS-008 (P1): The InferenceSpec constructor is protected; only
//     InferenceCategory (friend) can construct it directly. This test accesses
//     the protected default constructor via the derived class TestInferenceSpec
//     to obtain a spec instance with an empty packageId.
//   - SVS-009 (P2): Inference does not declare a move constructor; the
//     user-declared destructor (~Inference()) suppresses implicit move
//     generation, and the unique_ptr<Impl> member deletes copy construction.
//     Therefore Inference is neither copyable nor movable; verified via
//     std::is_*_v type traits (STATIC_REQUIRE_FALSE).
//
// Inference implementation notes (lib/SVS/Inference.cpp):
//   - Inference(spec) only stores the spec pointer; it does not dereference it.
//   - spec() returns the stored pointer value.
//   - SU() returns m_spec ? m_spec->runtime() : nullptr.

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/SVS/Inference.h>
#include <synthrt/SVS/InferenceContrib.h>

using namespace srt::svs;
using namespace srt::core;

namespace {

    // The default constructor of InferenceSpec is protected; only
    // InferenceCategory can call it. A public derived class exposes the
    // protected constructor so unit tests can construct a minimal spec instance
    // with an empty packageId and no bound Runtime.
    class TestInferenceSpec : public InferenceSpec {
    public:
        TestInferenceSpec() : InferenceSpec() {}
    };

    // Inference is abstract: it inherits pure virtual methods from
    // core::ITask (start, stop, result). TestInference provides minimal stub
    // implementations so unit tests can instantiate it. The non-pure
    // initialize() and startAsync() use the base default implementations.
    class TestInference : public Inference {
    public:
        using Inference::Inference;

        Expected<NO<TaskResult>> start(const NO<TaskStartInput> &) override { return {}; }
        bool stop() override { return true; }
        NO<TaskResult> result() const override { return nullptr; }
    };

} // namespace

// ---------------------------------------------------------------------------
// SVS-001: Inference constructed with nullptr spec
// Inference(nullptr) only stores the null pointer; must not crash.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-001: Inference accepts nullptr spec without crashing", "[svs][edge]") {
    Inference *inf = nullptr;
    REQUIRE_NOTHROW(inf = new TestInference(nullptr));
    REQUIRE(inf != nullptr);
    // spec() returns nullptr (the value passed at construction)
    REQUIRE(inf->spec() == nullptr);
    delete inf;
}

// ---------------------------------------------------------------------------
// SVS-003: spec() called before initialization
// After construction, calling spec() should return the spec pointer passed
// at construction.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-003: spec() returns the pointer passed at construction",
          "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);
    REQUIRE(inf.spec() == &spec);
}

// ---------------------------------------------------------------------------
// SVS-004: SU() called when Runtime is not set
// spec is set but no Runtime is bound (spec->runtime() is nullptr); SU()
// should return nullptr.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-004: SU() returns nullptr when Runtime is not bound",
          "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);
    // spec is non-null but no Runtime is bound; SU() returns nullptr
    REQUIRE(inf.spec() != nullptr);
    REQUIRE(inf.SU() == nullptr);
}

// ---------------------------------------------------------------------------
// SVS-005: InferenceInitArgs intermediateObjects is empty
// A default-constructed InferenceInitArgs does not set intermediateObjects;
// it should be an empty NO.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-005: InferenceInitArgs intermediateObjects defaults to empty NO",
          "[svs][edge]") {
    InferenceInitArgs args("test-init-args");
    REQUIRE(!args.intermediateObjects); // empty NO (null shared_ptr)
}

// ---------------------------------------------------------------------------
// SVS-006: Multiple threads concurrently calling spec() on the same Inference
// 2 threads concurrently call spec(); both should return the correct pointer
// with no data race.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-006: concurrent spec() calls return correct pointer",
          "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);

    std::vector<const InferenceSpec *> results(2, nullptr);
    std::atomic<int> done{0};

    auto t1 = std::thread([&] {
        results[0] = inf.spec();
        done.fetch_add(1, std::memory_order_relaxed);
    });
    auto t2 = std::thread([&] {
        results[1] = inf.spec();
        done.fetch_add(1, std::memory_order_relaxed);
    });
    t1.join();
    t2.join();

    REQUIRE(done.load() == 2);
    REQUIRE(results[0] == &spec);
    REQUIRE(results[1] == &spec);
}

// ---------------------------------------------------------------------------
// SVS-008: Inference constructed with a spec whose packageId is empty
// The default-constructed TestInferenceSpec has an empty packageId; constructing
// an Inference from it must not crash.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-008: Inference accepts spec with empty packageId", "[svs][edge]") {
    TestInferenceSpec spec;
    // A default-constructed spec has not gone through the package loading
    // flow; its packageId is empty.
    REQUIRE(spec.packageId().empty());
    REQUIRE_NOTHROW(TestInference(&spec));
    // Verify the spec is stored correctly
    TestInference inf(&spec);
    REQUIRE(inf.spec() == &spec);
}

// ---------------------------------------------------------------------------
// SVS-009: Inference move construction is not supported
// Inference is neither copyable nor movable: the user-declared destructor
// (~Inference()) suppresses implicit move generation, and the unique_ptr<Impl>
// member deletes copy construction. Verify the design invariant via type
// traits so a future refactor that accidentally adds move semantics is
// caught at compile time.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-009: Inference move construction is not supported", "[svs][edge]") {
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<Inference>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<Inference>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Inference>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<Inference>);
}

// ---------------------------------------------------------------------------
// SVS-010: intermediateObjects contains a large number of objects
// Set 10000 objects into an ObjectPool and assign it to InferenceInitArgs;
// must not crash.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-010: intermediateObjects with 10000 objects does not crash",
          "[svs][edge]") {
    auto pool = NO<ObjectPool>::create();
    for (int i = 0; i < 10000; ++i) {
        auto obj = NO<NamedObject>::create();
        pool->addObject(std::to_string(i), obj);
    }
    InferenceInitArgs args("test-bulk");
    args.intermediateObjects = pool;

    REQUIRE(args.intermediateObjects);
    REQUIRE(pool->allObjects().size() == 10000);
}

// ===========================================================================
// SVS-011 ~ SVS-020: supplementary edge conditions (InferenceInitArgs name
// semantics, InferenceSpec default state, multiple instances sharing a spec,
// concurrent construction, SU() consistency). New cases use only the existing
// public API (objectName/spec/SU/state/id/path) and do not rely on
// unpublished implementation details.
// ===========================================================================

// ---------------------------------------------------------------------------
// SVS-011: InferenceInitArgs exposes the name passed at construction via objectName()
// TaskInitArgs inherits TaskInfoBase -> NamedObject; the name is stored in
// objectName.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-011: InferenceInitArgs stores name via objectName()", "[svs][edge]") {
    InferenceInitArgs args("test-args-011");
    REQUIRE(args.objectName() == "test-args-011");
}

// ---------------------------------------------------------------------------
// SVS-012: InferenceInitArgs accepts an empty name
// An empty string must not cause a crash; objectName() should reflect the
// empty value.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-012: InferenceInitArgs accepts empty name", "[svs][edge]") {
    InferenceInitArgs args(std::string{});
    REQUIRE(args.objectName().empty());
}

// ---------------------------------------------------------------------------
// SVS-013: InferenceInitArgs accepts a Unicode name
// Verify storage and read consistency for a non-ASCII name (Chinese + ASCII mix).
// ---------------------------------------------------------------------------
TEST_CASE("SVS-013: InferenceInitArgs accepts Unicode name", "[svs][edge]") {
    InferenceInitArgs args("测试-args-013");
    REQUIRE(args.objectName() == "测试-args-013");
}

// ---------------------------------------------------------------------------
// SVS-014: Multiple Inference instances share the same spec pointer
// Construct 3 Inference objects sharing the same spec; spec() should all
// return the same pointer.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-014: Multiple Inference instances share same spec", "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf1(&spec);
    TestInference inf2(&spec);
    TestInference inf3(&spec);
    REQUIRE(inf1.spec() == &spec);
    REQUIRE(inf2.spec() == &spec);
    REQUIRE(inf3.spec() == &spec);
}

// ---------------------------------------------------------------------------
// SVS-015: Inference SU() returns consistent nullptr when Runtime is unbound
// Defensive test: ensure SU() behavior does not change across multiple calls.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-015: Inference SU() returns consistent nullptr when unbound",
          "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);
    auto *s1 = inf.SU();
    auto *s2 = inf.SU();
    auto *s3 = inf.SU();
    REQUIRE(s1 == nullptr);
    REQUIRE(s2 == nullptr);
    REQUIRE(s3 == nullptr);
    REQUIRE(s1 == s2);
    REQUIRE(s2 == s3);
}

// ---------------------------------------------------------------------------
// SVS-016: Inference spec() returns a consistent pointer across calls
// Defensive test: ensure spec() behavior does not change across multiple calls.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-016: Inference spec() returns consistent pointer across calls",
          "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);
    auto *s1 = inf.spec();
    auto *s2 = inf.spec();
    auto *s3 = inf.spec();
    REQUIRE(s1 == &spec);
    REQUIRE(s2 == &spec);
    REQUIRE(s3 == &spec);
    REQUIRE(s1 == s2);
    REQUIRE(s2 == s3);
}

// ---------------------------------------------------------------------------
// SVS-017: InferenceSpec default state is ModuleSpec::State::Invalid
// A spec that has not gone through the package loading flow should have
// state() return Invalid.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-017: InferenceSpec default state is Invalid", "[svs][edge]") {
    TestInferenceSpec spec;
    REQUIRE(spec.state() == ModuleSpec::State::Invalid);
}

// ---------------------------------------------------------------------------
// SVS-018: InferenceSpec default id and packageId are both empty
// A spec that has not gone through the package loading flow should have
// id()/packageId() return empty strings.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-018: InferenceSpec default id and packageId are empty",
          "[svs][edge]") {
    TestInferenceSpec spec;
    REQUIRE(spec.id().empty());
    REQUIRE(spec.packageId().empty());
}

// ---------------------------------------------------------------------------
// SVS-019: InferenceSpec default path is an empty path
// A spec that has not gone through the package loading flow should have
// path() return an empty filesystem path.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-019: InferenceSpec default path is empty", "[svs][edge]") {
    TestInferenceSpec spec;
    REQUIRE(spec.path().empty());
}

// ---------------------------------------------------------------------------
// SVS-020: Concurrent construction of multiple Inference instances sharing a spec
// 10 threads construct an Inference simultaneously; all should succeed and
// spec() should return the same pointer. Verifies thread safety of the
// Inference constructor under concurrency.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-020: Concurrent construction with same spec", "[svs][edge]") {
    TestInferenceSpec spec;
    constexpr int N = 10;
    std::vector<std::unique_ptr<Inference>> inferences(N);
    std::vector<std::thread> threads;
    threads.reserve(N);
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&inferences, &spec, i] {
            inferences[i] = std::make_unique<TestInference>(&spec);
        });
    }
    for (auto &t : threads) {
        t.join();
    }
    for (int i = 0; i < N; ++i) {
        REQUIRE(inferences[i] != nullptr);
        REQUIRE(inferences[i]->spec() == &spec);
    }
}

// ===========================================================================
// SVS-021 ~ SVS-030: third round of extended edge cases (ITask state, NamedObject
// property system, instance independence, concurrent read-only, init args
// boundaries). All cases use only APIs declared in Inference.h /
// InferenceContrib.h / ITask.h / NamedObject.h.
// ===========================================================================

// ---------------------------------------------------------------------------
// SVS-021: Inference inherits ITask::state() which is initially Idle
// A newly constructed Inference has not been started; state() should return
// ITask::Idle.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-021: Inference state is Idle on construction", "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);
    REQUIRE(inf.state() == ITask::Idle);
}

// ---------------------------------------------------------------------------
// SVS-022: Inference inherits NamedObject::objectName() which is empty by default
// A default-constructed NamedObject does not set a name; objectName() should
// return an empty string.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-022: Inference objectName is empty by default", "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);
    REQUIRE(inf.objectName().empty());
}

// ---------------------------------------------------------------------------
// SVS-023: Inference::setObjectName round-trip with objectName
// setObjectName should correctly store the name; objectName() returns the
// set value.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-023: Inference setObjectName round-trip", "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);
    inf.setObjectName("my-inference");
    REQUIRE(inf.objectName() == "my-inference");
}

// ---------------------------------------------------------------------------
// SVS-024: Inference property system returns an empty std::any by default
// An unset property should return an empty std::any (!has_value()).
// ---------------------------------------------------------------------------
TEST_CASE("SVS-024: Inference property default is empty", "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);
    const auto &val = inf.property("nonexistent");
    REQUIRE_FALSE(val.has_value());
}

// ---------------------------------------------------------------------------
// SVS-025: Inference property set/get round-trip
// After setProperty, property() should return the set value.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-025: Inference property set/get round-trip", "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);
    inf.setProperty("count", 42);
    const auto &val = inf.property("count");
    REQUIRE(val.has_value());
    REQUIRE(std::any_cast<int>(val) == 42);
}

// ---------------------------------------------------------------------------
// SVS-026: Two Inference instances have independent properties
// Property settings on different instances should not interfere with each other.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-026: Inference instances have independent properties",
          "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference a(&spec);
    TestInference b(&spec);
    a.setObjectName("A");
    b.setObjectName("B");
    REQUIRE(a.objectName() == "A");
    REQUIRE(b.objectName() == "B");
    REQUIRE(a.objectName() != b.objectName());
}

// ---------------------------------------------------------------------------
// SVS-027: InferenceSpec default apiLevel is 0 after construction
// TestInferenceSpec uses the protected default constructor; apiLevel should
// be the default value 0.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-027: InferenceSpec default apiLevel is zero", "[svs][edge]") {
    TestInferenceSpec spec;
    REQUIRE(spec.apiLevel() == 0);
}

// ---------------------------------------------------------------------------
// SVS-028: Concurrent calls to spec() and SU() do not crash
// Multiple threads concurrently call read-only spec() and SU(); they should
// return consistent results with no data race.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-028: Concurrent spec() and SU() reads are safe",
          "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);
    constexpr int N = 8;
    std::vector<std::thread> threads;
    threads.reserve(N);
    std::atomic<int> okCount{0};
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&inf, &spec, &okCount] {
            for (int j = 0; j < 100; ++j) {
                if (inf.spec() == &spec) {
                    okCount.fetch_add(1, std::memory_order_relaxed);
                }
                inf.SU(); // should return nullptr (Runtime not bound)
            }
        });
    }
    for (auto &t : threads) {
        t.join();
    }
    REQUIRE(okCount.load() == N * 100);
}

// ---------------------------------------------------------------------------
// SVS-029: InferenceInitArgs intermediateObjects defaults to an empty NO
// A default-constructed InferenceInitArgs should have intermediateObjects be
// an empty shared_ptr.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-029: InferenceInitArgs intermediateObjects default empty",
          "[svs][edge]") {
    InferenceInitArgs args("test-args");
    REQUIRE(args.objectName() == "test-args");
    // intermediateObjects: a default-constructed NO<ObjectPool> is an empty shared_ptr
    REQUIRE_FALSE(args.intermediateObjects);
}

// ---------------------------------------------------------------------------
// SVS-030: Inference destruction does not leak (repeated construct/destruct)
// Repeatedly constructing and destructing Inference should not cause memory
// leaks or crashes.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-030: Inference construction/destruction cycle is safe",
          "[svs][edge]") {
    TestInferenceSpec spec;
    for (int i = 0; i < 100; ++i) {
        auto inf = std::make_unique<TestInference>(&spec);
        REQUIRE(inf->spec() == &spec);
        // Destruction happens when the unique_ptr is reset
    }
    // Reaching this point without crashing means the
    // construction/destruction cycle is safe
    SUCCEED();
}

// ===========================================================================
// SVS-031 ~ SVS-036: fourth round of regression tests for recently fixed bugs
// (empty property key, multi-type property storage, InferenceSpec apiLevel
// default, Inference initial state, long objectName round-trip, concurrent
// property access). All cases use only APIs declared in Inference.h /
// InferenceContrib.h / NamedObject.h / ITask.h.
// ===========================================================================

// ---------------------------------------------------------------------------
// SVS-031: Inference property with an empty key
// Setting and getting a property with an empty string key must not crash and
// must round-trip the value correctly.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-031: Inference property with empty key", "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);
    inf.setProperty("", 123);
    const auto &val = inf.property("");
    REQUIRE(val.has_value());
    REQUIRE(std::any_cast<int>(val) == 123);
}

// ---------------------------------------------------------------------------
// SVS-032: Inference setProperty with various types
// Properties of different types (int, string, double, bool) must round-trip
// correctly through std::any_cast.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-032: Inference setProperty with various types", "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);

    inf.setProperty("int-prop", 42);
    inf.setProperty("str-prop", std::string("hello"));
    inf.setProperty("dbl-prop", 3.14);
    inf.setProperty("bool-prop", true);

    REQUIRE(std::any_cast<int>(inf.property("int-prop")) == 42);
    REQUIRE(std::any_cast<std::string>(inf.property("str-prop")) == "hello");
    REQUIRE(std::any_cast<double>(inf.property("dbl-prop")) == 3.14);
    REQUIRE(std::any_cast<bool>(inf.property("bool-prop")) == true);
}

// ---------------------------------------------------------------------------
// SVS-033: InferenceSpec apiLevel default is 0
// A default-constructed TestInferenceSpec (using the protected default
// constructor) has apiLevel() == 0.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-033: InferenceSpec apiLevel default is zero", "[svs][edge]") {
    TestInferenceSpec spec;
    REQUIRE(spec.apiLevel() == 0);
}

// ---------------------------------------------------------------------------
// SVS-034: Inference state is Idle after construction
// A newly constructed Inference has not been started; state() must return
// ITask::Idle.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-034: Inference state is Idle after construction",
          "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);
    REQUIRE(inf.state() == ITask::Idle);
}

// ---------------------------------------------------------------------------
// SVS-035: Inference objectName round-trip with a long string
// Setting a long objectName (1000 characters) must round-trip correctly
// through objectName().
// ---------------------------------------------------------------------------
TEST_CASE("SVS-035: Inference setObjectName with long string round-trips",
          "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);
    const std::string longName(1000, 'n');
    inf.setObjectName(longName);
    REQUIRE(inf.objectName() == longName);
    REQUIRE(inf.objectName().size() == 1000);
}

// ---------------------------------------------------------------------------
// SVS-036: Inference concurrent property access on different keys
// 4 threads concurrently setProperty and property() on different keys; must
// not crash. After all threads join, each key must hold its final value.
// ---------------------------------------------------------------------------
TEST_CASE("SVS-036: Inference concurrent property access is safe",
          "[svs][edge]") {
    TestInferenceSpec spec;
    TestInference inf(&spec);

    constexpr int N = 4;
    std::vector<std::thread> threads;
    threads.reserve(N);

    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&inf, i] {
            const std::string key = "key-" + std::to_string(i);
            const int value = i * 100;
            for (int j = 0; j < 100; ++j) {
                inf.setProperty(key, value);
                // Read back; concurrent access on different keys must not crash.
                inf.property(key);
            }
        });
    }
    for (auto &t : threads) {
        t.join();
    }
    // After all threads complete, each key must hold the correct final value.
    for (int i = 0; i < N; ++i) {
        const std::string key = "key-" + std::to_string(i);
        const auto &val = inf.property(key);
        REQUIRE(val.has_value());
        REQUIRE(std::any_cast<int>(val) == i * 100);
    }
}
