// lib/Driver edge condition test cases (DRV-001 ~ DRV-010)
//
// Covers srt::driver::DriverRegistry and InferenceDriver behavior under abnormal
// input, duplicate registration and concurrency scenarios. See the task
// description for the test matrix; corresponds to the implementation in
// lib/Driver/DriverRegistry.cpp.
//
// === API differences vs. the test matrix ===
// The following cases have actual return values that differ from the matrix
// description (adjusted to the actual API):
//   - DRV-001 (P0): The matrix expects Error(AlreadyExists). The actual
//     registerDriver returns Error::InvalidArgument ("driver name already
//     registered") for duplicate names; there is no dedicated AlreadyExists
//     error type (lib/Driver/DriverRegistry.cpp:16-20).
//   - DRV-007 (P2): The matrix expects "documented as UB". DriverRegistry does
//     not take ownership of driver pointers; using a dangling pointer after
//     unregister is UB, documented without a test shell.
//   - DRV-008 (P1): The matrix expects "Error(InvalidArg) or allowed". The
//     actual registerDriver does not validate empty names; an empty name is
//     accepted as a valid key (returns success).
//
// DriverRegistry thread safety policy (lib/Driver/DriverRegistry.h):
//   - registerDriver / unregisterDriver: exclusive (unique_lock)
//   - driver / driverNames / hasDriver: shared (shared_lock)

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/DriverRegistry.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>

using namespace srt::driver;
using srt::core::Error;
using srt::core::Expected;
using srt::core::NO;

namespace {

    // InferenceDriver is a pure virtual interface; a minimal mock implementation
    // is required for DriverRegistry testing. NamedObject is an indirect base
    // (via InferenceDriver), so the name is set via setObjectName() in the body.
    class MockInferenceDriver : public InferenceDriver {
    public:
        explicit MockInferenceDriver(std::string name) {
            setObjectName(std::move(name));
        }

        std::string arch() const override { return "mock-arch"; }
        std::string backend() const override { return "mock-backend"; }

        Expected<void> initialize(
            const NO<InferenceDriverInitArgs> & /*args*/) override {
            return {};
        }

        NO<InferenceSession> createSession() override { return nullptr; }
    };

} // namespace

// ---------------------------------------------------------------------------
// DRV-001/002: registerDriver error cases (duplicate name, nullptr)
// Merged: both verify the InvalidArgument rejection contract and share the
// DriverRegistry construction. SECTION form preserves per-case
// traceability while removing the duplicate setup.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-001/002: registerDriver rejects invalid arguments",
          "[driver][edge]") {
    DriverRegistry registry;

    SECTION("DRV-001: duplicate name -> InvalidArgument") {
        // API difference: actually returns Error::InvalidArgument (no
        // dedicated AlreadyExists).
        MockInferenceDriver driver1("dsdriver");
        MockInferenceDriver driver2("dsdriver");

        REQUIRE(registry.registerDriver("dsdriver", &driver1).hasValue());

        auto result = registry.registerDriver("dsdriver", &driver2);
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(result.error().type() == Error::InvalidArgument);
        REQUIRE_FALSE(result.error().message().empty());
    }

    SECTION("DRV-002: nullptr driver -> InvalidArgument") {
        auto result = registry.registerDriver("name", nullptr);
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(result.error().type() == Error::InvalidArgument);
    }
}

// ---------------------------------------------------------------------------
// DRV-003/004/009: unknown-name lookup / empty registry queries
// Merged: all three cases share the DriverRegistry construction and verify
// the empty/unknown-name contract. SECTION form preserves per-API
// traceability while removing the duplicate setup. Adds an empty-name lookup
// boundary case.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-003/004/009: unknown-name lookup on empty registry",
          "[driver][edge]") {
    DriverRegistry registry;

    SECTION("DRV-003: unregisterDriver unknown name returns false") {
        REQUIRE_FALSE(registry.unregisterDriver("nonexistent"));
    }

    SECTION("DRV-004: driver unknown name returns nullptr") {
        REQUIRE(registry.driver("nonexistent") == nullptr);
    }

    SECTION("DRV-009: driverNames on empty registry returns empty vector") {
        auto names = registry.driverNames();
        REQUIRE(names.empty());
        // drivers() should also be empty
        REQUIRE(registry.drivers().empty());
    }

    SECTION("DRV-004b: hasDriver unknown name returns false") {
        // Boundary: hasDriver on an unregistered name must return false.
        REQUIRE_FALSE(registry.hasDriver("nonexistent"));
    }
}

// ---------------------------------------------------------------------------
// DRV-005: concurrent registerDriver with different names
// 2 threads register different names simultaneously; both should succeed.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-005: concurrent registerDriver with different names",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver driver1("drv-a");
    MockInferenceDriver driver2("drv-b");

    std::atomic<int> okCount{0};
    auto t1 = std::thread([&] {
        if (registry.registerDriver("a", &driver1).hasValue())
            okCount.fetch_add(1, std::memory_order_relaxed);
    });
    auto t2 = std::thread([&] {
        if (registry.registerDriver("b", &driver2).hasValue())
            okCount.fetch_add(1, std::memory_order_relaxed);
    });
    t1.join();
    t2.join();

    REQUIRE(okCount.load() == 2);
    REQUIRE(registry.hasDriver("a"));
    REQUIRE(registry.hasDriver("b"));
}

// ---------------------------------------------------------------------------
// DRV-006: concurrent driver() read-only lookups
// N threads call driver("name") simultaneously; all should return the correct pointer.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-006: concurrent driver() lookups return correct pointer",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver driver("drv-x");
    registry.registerDriver("x", &driver);

    constexpr int N = 4;
    constexpr int ITERS = 200;
    std::atomic<int> matchCount{0};

    auto worker = [&] {
        for (int i = 0; i < ITERS; ++i) {
            if (registry.driver("x") == &driver)
                matchCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(N);
    for (int i = 0; i < N; ++i)
        threads.emplace_back(worker);
    for (auto &t : threads)
        t.join();

    REQUIRE(matchCount.load() == N * ITERS);
}

// ---------------------------------------------------------------------------
// DRV-008: registerDriver with empty name
// API difference: empty names are actually allowed (no validation; treated as a valid key)
// ---------------------------------------------------------------------------
TEST_CASE("DRV-008: registerDriver with empty name is allowed", "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver driver("drv-empty");
    // Matrix allows Error(InvalidArg) or success; actual implementation does not validate empty names and returns success
    auto result = registry.registerDriver("", &driver);
    REQUIRE(result.hasValue());
    REQUIRE(registry.hasDriver(""));
    REQUIRE(registry.driver("") == &driver);
}

// ---------------------------------------------------------------------------
// DRV-010: concurrent registerDriver and unregisterDriver
// Thread 1 registers "b", thread 2 unregisters "a" (pre-registered). Both
// succeed without deadlock.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-010: concurrent register and unregister without deadlock",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver driverA("drv-a");
    MockInferenceDriver driverB("drv-b");
    // Pre-register "a" for thread 2 to unregister
    REQUIRE(registry.registerDriver("a", &driverA).hasValue());

    std::atomic<bool> regOk{false};
    std::atomic<bool> unregOk{false};

    auto t1 = std::thread([&] {
        regOk = registry.registerDriver("b", &driverB).hasValue();
    });
    auto t2 = std::thread([&] {
        unregOk = registry.unregisterDriver("a");
    });
    t1.join();
    t2.join();

    // Both operations completed successfully (no deadlock)
    REQUIRE(regOk.load());
    REQUIRE(unregOk.load());
    REQUIRE(registry.hasDriver("b"));
    REQUIRE_FALSE(registry.hasDriver("a"));
}

// ===========================================================================
// DRV-011 ~ DRV-018: supplementary DriverRegistry edge coverage (re-registration,
// multi-driver enumeration, read/write stability under concurrency,
// MockInferenceDriver behavior baseline).
// ===========================================================================

// ---------------------------------------------------------------------------
// DRV-011: re-registering the same name after unregister should succeed
// unregisterDriver releases the name; a subsequent registerDriver with the
// same name should succeed.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-011: re-register same name after unregister succeeds",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver driver1("drv-r");
    MockInferenceDriver driver2("drv-r2");

    REQUIRE(registry.registerDriver("name", &driver1).hasValue());
    REQUIRE(registry.unregisterDriver("name"));
    // Re-registering the same name with a different driver instance should succeed
    auto result = registry.registerDriver("name", &driver2);
    REQUIRE(result.hasValue());
    REQUIRE(registry.driver("name") == &driver2);
}

// ---------------------------------------------------------------------------
// DRV-012: after registering multiple drivers, driverNames returns all names
// Register 3 drivers with different names; driverNames() should contain all 3.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-012: driverNames returns all registered names",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver d1("drv-1");
    MockInferenceDriver d2("drv-2");
    MockInferenceDriver d3("drv-3");

    REQUIRE(registry.registerDriver("n1", &d1).hasValue());
    REQUIRE(registry.registerDriver("n2", &d2).hasValue());
    REQUIRE(registry.registerDriver("n3", &d3).hasValue());

    auto names = registry.driverNames();
    REQUIRE(names.size() == 3);
    // unordered_map order is not guaranteed, so use set-membership checking
    int hits = 0;
    for (const auto &n : names) {
        if (n == "n1" || n == "n2" || n == "n3") {
            ++hits;
        }
    }
    REQUIRE(hits == 3);
}

// ---------------------------------------------------------------------------
// DRV-013: hasDriver returns false on an empty registry
// Defensive test: querying any name on an empty registry returns false, no crash.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-013: hasDriver returns false on empty registry", "[driver][edge]") {
    DriverRegistry registry;
    REQUIRE_FALSE(registry.hasDriver("any"));
    REQUIRE_FALSE(registry.hasDriver(""));
}

// ---------------------------------------------------------------------------
// DRV-014: drivers() returns all registered driver pointers
// Register 2 drivers; drivers() should return 2 pointers containing both.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-014: drivers() returns all registered pointers", "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver d1("drv-a");
    MockInferenceDriver d2("drv-b");

    REQUIRE(registry.registerDriver("a", &d1).hasValue());
    REQUIRE(registry.registerDriver("b", &d2).hasValue());

    auto drvPtrs = registry.drivers();
    REQUIRE(drvPtrs.size() == 2);

    int foundA = 0, foundB = 0;
    for (auto *p : drvPtrs) {
        if (p == &d1) ++foundA;
        if (p == &d2) ++foundB;
    }
    REQUIRE(foundA == 1);
    REQUIRE(foundB == 1);
}

// ---------------------------------------------------------------------------
// DRV-015: the same driver pointer registered under two different names
// DriverRegistry does not validate pointer uniqueness; the same pointer may be
// bound to multiple names.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-015: same driver pointer registered under two names",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver driver("drv-shared");

    REQUIRE(registry.registerDriver("alias1", &driver).hasValue());
    REQUIRE(registry.registerDriver("alias2", &driver).hasValue());

    REQUIRE(registry.driver("alias1") == &driver);
    REQUIRE(registry.driver("alias2") == &driver);
    // Unregistering one name does not affect the other
    REQUIRE(registry.unregisterDriver("alias1"));
    REQUIRE(registry.driver("alias1") == nullptr);
    REQUIRE(registry.driver("alias2") == &driver);
}

// ---------------------------------------------------------------------------
// DRV-016: MockInferenceDriver arch()/backend() return mock baseline values
// Ensure mock behavior is stable: arch() == "mock-arch", backend() == "mock-backend".
// ---------------------------------------------------------------------------
TEST_CASE("DRV-016: MockInferenceDriver returns mock arch/backend values",
          "[driver][edge]") {
    MockInferenceDriver driver("drv-mock");
    REQUIRE(driver.arch() == "mock-arch");
    REQUIRE(driver.backend() == "mock-backend");
    // The NamedObject base class objectName should reflect the name passed at construction
    REQUIRE(driver.objectName() == "drv-mock");
}

// ---------------------------------------------------------------------------
// DRV-017: read/write stability under heavy concurrent iteration
// 1 thread continuously registers/unregisters, 3 threads continuously call
// driver() lookups. Verify no deadlock, no crash, and a consistent final
// registry state.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-017: concurrent read-write stress without deadlock",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver driver("drv-stress");
    constexpr int ITERS = 200;

    std::atomic<bool> stop{false};
    std::atomic<int> readCount{0};

    auto reader = [&] {
        while (!stop.load(std::memory_order_relaxed)) {
            if (registry.driver("stress-name") != nullptr) {
                readCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> readers;
    readers.reserve(3);
    for (int i = 0; i < 3; ++i) {
        readers.emplace_back(reader);
    }

    // Writer thread: repeatedly register / unregister
    for (int i = 0; i < ITERS; ++i) {
        registry.registerDriver("stress-name", &driver);
        registry.unregisterDriver("stress-name");
    }
    stop.store(true, std::memory_order_relaxed);
    for (auto &t : readers) {
        t.join();
    }
    // Verify final state: driver should have been unregistered
    REQUIRE_FALSE(registry.hasDriver("stress-name"));
    // Reader threads should observe at least one non-null result (timing-sensitive,
    // but near-certain with large ITERS). We do not assert readCount > 0 here to
    // avoid timing dependence.
}

// ---------------------------------------------------------------------------
// DRV-018: registerDriver then unregisterDriver twice; second call returns false
// Defensive test: the second unregister should return false (name is no longer
// occupied) and not crash.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-018: double unregister returns false on second call",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver driver("drv-double");
    REQUIRE(registry.registerDriver("name", &driver).hasValue());
    REQUIRE(registry.unregisterDriver("name"));
    // Second unregister: name is no longer occupied, returns false
    REQUIRE_FALSE(registry.unregisterDriver("name"));
}

// ===========================================================================
// DRV-019 ~ DRV-028: third round of extended edge cases (driverNames
// completeness, drivers vector, empty-name registration, coverage updates,
// concurrent enumeration). All cases use only APIs declared in
// DriverRegistry.h / InferenceDriver.h.
// ===========================================================================

// ---------------------------------------------------------------------------
// DRV-019: driverNames returns all registered names
// After registering 3 drivers, driverNames() should return 3 names.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-019: driverNames returns all registered names",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver d1("drv-a");
    MockInferenceDriver d2("drv-b");
    MockInferenceDriver d3("drv-c");

    REQUIRE(registry.registerDriver("a", &d1).hasValue());
    REQUIRE(registry.registerDriver("b", &d2).hasValue());
    REQUIRE(registry.registerDriver("c", &d3).hasValue());

    auto names = registry.driverNames();
    REQUIRE(names.size() == 3);
    // unordered_map order is not guaranteed, but all names should be in the list
    bool hasA = false, hasB = false, hasC = false;
    for (const auto &n : names) {
        if (n == "a") hasA = true;
        if (n == "b") hasB = true;
        if (n == "c") hasC = true;
    }
    REQUIRE(hasA);
    REQUIRE(hasB);
    REQUIRE(hasC);
}

// ---------------------------------------------------------------------------
// DRV-020: drivers() returns all registered driver pointers
// After registering 2 drivers, drivers() should return 2 non-null pointers.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-020: drivers() returns all registered pointers",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver d1("drv-x");
    MockInferenceDriver d2("drv-y");

    REQUIRE(registry.registerDriver("x", &d1).hasValue());
    REQUIRE(registry.registerDriver("y", &d2).hasValue());

    auto drvList = registry.drivers();
    REQUIRE(drvList.size() == 2);
    // Pointers should be non-null
    for (auto *ptr : drvList) {
        REQUIRE(ptr != nullptr);
    }
}

// ---------------------------------------------------------------------------
// DRV-021: registerDriver accepts an empty name
// The implementation does not validate empty names; an empty string is accepted
// as a valid key.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-021: registerDriver accepts empty name", "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver driver("drv-empty");
    auto result = registry.registerDriver("", &driver);
    REQUIRE(result.hasValue());
    REQUIRE(registry.hasDriver(""));
    REQUIRE(registry.driver("") == &driver);
}

// ---------------------------------------------------------------------------
// DRV-022: registerDriver duplicate name returns an error
// Registering the same name a second time should return an InvalidArgument error.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-022: registerDriver duplicate name returns error",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver d1("drv-dup1");
    MockInferenceDriver d2("drv-dup2");

    REQUIRE(registry.registerDriver("dup-name", &d1).hasValue());
    auto result = registry.registerDriver("dup-name", &d2);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error().code() == srt::core::ErrorCode::InvalidArgument);
    // The first driver remains registered
    REQUIRE(registry.driver("dup-name") == &d1);
}

// ---------------------------------------------------------------------------
// DRV-023: hasDriver returns false on an empty registry
// ---------------------------------------------------------------------------
TEST_CASE("DRV-023: hasDriver on empty registry returns false",
          "[driver][edge]") {
    DriverRegistry registry;
    REQUIRE_FALSE(registry.hasDriver("anything"));
    REQUIRE_FALSE(registry.hasDriver(""));
}

// ---------------------------------------------------------------------------
// DRV-024: driverNames on an empty registry returns an empty vector
// ---------------------------------------------------------------------------
TEST_CASE("DRV-024: driverNames on empty registry returns empty",
          "[driver][edge]") {
    DriverRegistry registry;
    auto names = registry.driverNames();
    REQUIRE(names.empty());
}

// ---------------------------------------------------------------------------
// DRV-025: drivers() on an empty registry returns an empty vector
// ---------------------------------------------------------------------------
TEST_CASE("DRV-025: drivers() on empty registry returns empty",
          "[driver][edge]") {
    DriverRegistry registry;
    auto drvList = registry.drivers();
    REQUIRE(drvList.empty());
}

// ---------------------------------------------------------------------------
// DRV-026: re-registering the same name after unregister succeeds
// After unregisterDriver, the same name key can be registered again.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-026: re-register after unregister succeeds", "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver d1("drv-rereg1");
    MockInferenceDriver d2("drv-rereg2");

    REQUIRE(registry.registerDriver("slot", &d1).hasValue());
    REQUIRE(registry.unregisterDriver("slot"));
    // Re-register the same name
    REQUIRE(registry.registerDriver("slot", &d2).hasValue());
    REQUIRE(registry.driver("slot") == &d2);
}

// ---------------------------------------------------------------------------
// DRV-027: driver() returns nullptr after unregister
// ---------------------------------------------------------------------------
TEST_CASE("DRV-027: driver() returns nullptr after unregister",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver driver("drv-post");
    REQUIRE(registry.registerDriver("post", &driver).hasValue());
    REQUIRE(registry.driver("post") == &driver);

    registry.unregisterDriver("post");
    REQUIRE(registry.driver("post") == nullptr);
    REQUIRE_FALSE(registry.hasDriver("post"));
}

// ---------------------------------------------------------------------------
// DRV-028: concurrent driverNames() reads do not block
// Multiple threads calling driverNames() simultaneously should all succeed
// without deadlock.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-028: concurrent driverNames() reads are safe",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver d1("drv-conc1");
    MockInferenceDriver d2("drv-conc2");
    MockInferenceDriver d3("drv-conc3");
    registry.registerDriver("c1", &d1);
    registry.registerDriver("c2", &d2);
    registry.registerDriver("c3", &d3);

    constexpr int N = 8;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    threads.reserve(N);
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&registry, &successCount] {
            for (int j = 0; j < 50; ++j) {
                auto names = registry.driverNames();
                if (names.size() == 3) {
                    successCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto &t : threads) {
        t.join();
    }
    REQUIRE(successCount.load() == N * 50);
}

// ===========================================================================
// DRV-029 ~ DRV-035: fourth round of regression tests for recently fixed bugs
// (destructor-outside-lock deadlock, re-registration semantics, long-name
// handling, concurrent enumeration, iterator invalidation, non-owning pointer
// semantics, mock baseline). All cases use only APIs declared in
// DriverRegistry.h / InferenceDriver.h.
// ===========================================================================

// ---------------------------------------------------------------------------
// DRV-029: unregister (entry destruction) outside lock does not deadlock
// Regression test for the HandleTable fix where internal cleanup must run
// outside the registry lock. One thread continuously looks up a driver via
// the shared lock while another thread unregisters it via the exclusive lock;
// the unregister must not deadlock with the concurrent shared-lock lookup.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-029: unregister during concurrent lookup does not deadlock",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver driver("drv-deadlock");

    REQUIRE(registry.registerDriver("deadlock-name", &driver).hasValue());

    std::atomic<bool> stop{false};
    std::atomic<int> lookups{0};

    auto reader = std::thread([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            // shared_lock lookup; must not block on the unregister's exclusive lock
            if (registry.driver("deadlock-name") != nullptr) {
                lookups.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    // Unregister (exclusive lock) while the reader contends for the shared
    // lock. If cleanup ran under the lock this could deadlock; the fix ensures
    // it happens outside the lock.
    REQUIRE(registry.unregisterDriver("deadlock-name"));

    stop.store(true, std::memory_order_relaxed);
    reader.join();
    // Reaching this point means no deadlock occurred.
    REQUIRE_FALSE(registry.hasDriver("deadlock-name"));
}

// ---------------------------------------------------------------------------
// DRV-030: register then unregister then register the same name
// After unregistering "name" (bound to driver1), re-registering "name" with
// driver2 should succeed and driver("name") should return driver2.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-030: register/unregister/register same name returns new driver",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver driver1("drv-first");
    MockInferenceDriver driver2("drv-second");

    REQUIRE(registry.registerDriver("name", &driver1).hasValue());
    REQUIRE(registry.driver("name") == &driver1);

    REQUIRE(registry.unregisterDriver("name"));

    REQUIRE(registry.registerDriver("name", &driver2).hasValue());
    REQUIRE(registry.driver("name") == &driver2);
    REQUIRE(registry.driver("name") != &driver1);
}

// ---------------------------------------------------------------------------
// DRV-031: DriverRegistry accepts a very long driver name
// Register a driver with a 1000-character name; lookup and enumeration must
// succeed.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-031: registerDriver accepts very long name", "[driver][edge]") {
    DriverRegistry registry;
    const std::string longName(1000, 'x');
    MockInferenceDriver driver("drv-long");

    REQUIRE(registry.registerDriver(longName, &driver).hasValue());
    REQUIRE(registry.hasDriver(longName));
    REQUIRE(registry.driver(longName) == &driver);

    auto names = registry.driverNames();
    REQUIRE(names.size() == 1);
    REQUIRE(names.front() == longName);
    REQUIRE(names.front().size() == 1000);
}

// ---------------------------------------------------------------------------
// DRV-032: concurrent registerDriver and driverNames() do not crash or corrupt
// One thread registers drivers (exclusive lock) while another repeatedly calls
// driverNames() (shared lock). The returned snapshots must be internally
// consistent (size never exceeds the total registered count) and neither call
// may crash.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-032: concurrent register and driverNames is safe",
          "[driver][edge]") {
    DriverRegistry registry;
    std::vector<std::unique_ptr<MockInferenceDriver>> drivers;
    drivers.reserve(50);
    for (int i = 0; i < 50; ++i) {
        drivers.emplace_back(std::make_unique<MockInferenceDriver>(
            "drv-" + std::to_string(i)));
    }

    std::atomic<bool> stop{false};
    std::atomic<int> namesCalls{0};
    std::atomic<bool> corrupt{false};

    auto enumerator = std::thread([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            auto names = registry.driverNames();
            // The returned vector is a snapshot; its size must never exceed
            // the total number of registered drivers.
            if (names.size() > 50) {
                corrupt.store(true, std::memory_order_relaxed);
            }
            namesCalls.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Wait for the enumerator to start running before registering, so that
    // registration genuinely overlaps with concurrent driverNames() calls.
    // Without this, a fast registration loop can finish (and set stop) before
    // the enumerator thread gets its first time slice, leaving namesCalls == 0.
    while (namesCalls.load(std::memory_order_relaxed) == 0) {
        std::this_thread::yield();
    }

    for (int i = 0; i < 50; ++i) {
        REQUIRE(registry.registerDriver("d" + std::to_string(i),
                                        drivers[i].get())
                    .hasValue());
    }

    stop.store(true, std::memory_order_relaxed);
    enumerator.join();

    // Final state: all 50 drivers registered, no crash, no corruption.
    REQUIRE_FALSE(corrupt.load());
    REQUIRE(registry.driverNames().size() == 50);
    REQUIRE(namesCalls.load() > 0);
}

// ---------------------------------------------------------------------------
// DRV-033: unregister during iteration over a driverNames() snapshot
// driverNames() returns a vector by value (a copy of the internal name list),
// so unregistering a driver while iterating over the returned snapshot must
// not invalidate the iterator.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-033: unregister during iteration over driverNames snapshot",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver d1("drv-a");
    MockInferenceDriver d2("drv-b");
    MockInferenceDriver d3("drv-c");

    REQUIRE(registry.registerDriver("a", &d1).hasValue());
    REQUIRE(registry.registerDriver("b", &d2).hasValue());
    REQUIRE(registry.registerDriver("c", &d3).hasValue());

    auto names = registry.driverNames();
    REQUIRE(names.size() == 3);

    // Unregister "b" while iterating over the snapshot. The snapshot is a
    // copy, so the iteration must not be invalidated.
    int iterated = 0;
    for (const auto &n : names) {
        if (iterated == 1) {
            REQUIRE(registry.unregisterDriver("b"));
        }
        ++iterated;
        // Each name in the snapshot must be one of the originally registered names
        REQUIRE((n == "a" || n == "b" || n == "c"));
    }
    REQUIRE(iterated == 3);

    // After the loop, "b" is unregistered but "a" and "c" remain.
    REQUIRE_FALSE(registry.hasDriver("b"));
    REQUIRE(registry.hasDriver("a"));
    REQUIRE(registry.hasDriver("c"));
}

// ---------------------------------------------------------------------------
// DRV-034: same driver pointer registered under two names; unregistering one
// does not affect the other (non-owning semantics).
// Register the same driver under "a" and "b"; after unregistering "a", "b"
// must still resolve to the same pointer.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-034: unregister one alias keeps the other alive",
          "[driver][edge]") {
    DriverRegistry registry;
    MockInferenceDriver driver("drv-shared");

    REQUIRE(registry.registerDriver("a", &driver).hasValue());
    REQUIRE(registry.registerDriver("b", &driver).hasValue());

    REQUIRE(registry.driver("a") == &driver);
    REQUIRE(registry.driver("b") == &driver);

    // Unregister "a"; "b" must still resolve to the same pointer.
    REQUIRE(registry.unregisterDriver("a"));
    REQUIRE(registry.driver("a") == nullptr);
    REQUIRE(registry.driver("b") == &driver);
    REQUIRE(registry.hasDriver("b"));
}

// ---------------------------------------------------------------------------
// DRV-035: MockInferenceDriver createSession returns nullptr (baseline)
// The mock does not create a real session; createSession() must return an
// empty NO (nullptr) as a baseline behavior.
// ---------------------------------------------------------------------------
TEST_CASE("DRV-035: MockInferenceDriver createSession returns nullptr",
          "[driver][edge]") {
    MockInferenceDriver driver("drv-session");
    auto session = driver.createSession();
    REQUIRE(!session);
    REQUIRE(session.get() == nullptr);
}
