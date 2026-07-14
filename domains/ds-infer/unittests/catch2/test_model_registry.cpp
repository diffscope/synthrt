// Unit tests for ds::infer::ModelRegistry.
//
// BF-31: ModelRegistry now caches by composite key (packageId, inferenceId)
// so that two packages defining inferences with the same id (e.g. "pitch")
// keep independent sessions (ARCH-06 cross-package stage sharing).
// Previously, bind() keyed by inferenceId only, so the second package's
// bind() would incorrectly return the first package's cached session.
//
// Uses a MockInferenceDriver / MockInferenceSession to avoid pulling in the
// real ONNX Runtime driver. The mock session records the model paths opened
// so tests can assert which manifest's models were loaded.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Task/ITask.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>

#include <diffsinger/Bank/PackageManifest.h>

#include "ModelRegistry.h"

using namespace ds::infer;
using namespace ds::bank;

namespace {

    // Minimal InferenceSession mock: records every open() path and assigns
    // a unique id so two sessions can be told apart. All ITask pure virtuals
    // return errors / defaults — ModelRegistry::bind() never calls start/stop.
    class MockSession : public srt::driver::InferenceSession {
    public:
        explicit MockSession(int64_t sid) : m_id(sid) {}

        srt::core::Expected<void> open(const std::filesystem::path &path,
                                       const srt::core::NO<srt::driver::InferenceSessionOpenArgs> &) override {
            m_openedPaths.push_back(path);
            return srt::core::Expected<void>();
        }
        srt::core::Expected<void> close() override { return srt::core::Expected<void>(); }
        bool isOpen() const override { return true; }
        int64_t id() const override { return m_id; }

        // ITask pure virtuals — not exercised by ModelRegistry.
        srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
            start(const srt::core::NO<srt::core::TaskStartInput> &) override {
            return srt::core::Error(srt::core::Error::NotImplemented,
                                    "MockSession::start not implemented");
        }
        bool stop() override { return false; }
        srt::core::NO<srt::core::TaskResult> result() const override { return nullptr; }

        const std::vector<std::filesystem::path> &openedPaths() const { return m_openedPaths; }

    private:
        int64_t m_id;
        std::vector<std::filesystem::path> m_openedPaths;
    };

    // Minimal InferenceDriver mock: hands out MockSession instances with
    // monotonically increasing ids so each bind() that creates a new session
    // produces a distinguishable object.
    class MockDriver : public srt::driver::InferenceDriver {
    public:
        MockDriver() {
            setObjectName("mock");
        }

        std::string arch() const override { return "mock"; }
        std::string backend() const override { return "mock"; }
        srt::core::Expected<void> initialize(
            const srt::core::NO<srt::driver::InferenceDriverInitArgs> &) override {
            return srt::core::Expected<void>();
        }
        srt::core::NO<srt::driver::InferenceSession> createSession() override {
            const auto sid = m_nextId.fetch_add(1);
            return srt::core::NO<srt::driver::InferenceSession>(new MockSession(sid));
        }

        // Counter used to verify how many sessions were created.
        int64_t sessionsCreated() const { return m_nextId.load(); }

    private:
        std::atomic<int64_t> m_nextId{0};
    };

    // Build an InferenceInfo with a single model path. modelPaths is a
    // map<string, path> keyed by the model name.
    InferenceInfo makeManifest(const std::string &id,
                               const std::string &packageId,
                               const std::string &modelPath) {
        InferenceInfo info;
        info.id = id;
        info.packageId = packageId;
        info.modelPaths["main"] = modelPath;
        return info;
    }

} // namespace

// ---------------------------------------------------------------------------
// bind() caching behavior
// ---------------------------------------------------------------------------

TEST_CASE("ModelRegistry bind returns cached session on repeat",
          "[modelregistry]") {
    MockDriver driver;
    ModelRegistry registry;

    auto manifest = makeManifest("pitch", "com.test.A", "/models/pitch_a.onnx");
    auto r1 = registry.bind(manifest, &driver);
    REQUIRE(r1.hasValue());
    REQUIRE(r1.value() != nullptr);
    const auto sessionsBefore = driver.sessionsCreated();

    // Repeat bind for the same (packageId, inferenceId) must return the
    // cached session without creating a new one.
    auto r2 = registry.bind(manifest, &driver);
    REQUIRE(r2.hasValue());
    REQUIRE(r2.value().get() == r1.value().get());
    REQUIRE(driver.sessionsCreated() == sessionsBefore);
}

TEST_CASE("ModelRegistry bind null driver returns error",
          "[modelregistry]") {
    ModelRegistry registry;
    auto manifest = makeManifest("pitch", "com.test.A", "/models/pitch.onnx");
    auto r = registry.bind(manifest, nullptr);
    REQUIRE(!r.hasValue());
}

// ---------------------------------------------------------------------------
// getBoundSession lookup
// ---------------------------------------------------------------------------

TEST_CASE("ModelRegistry getBoundSession returns bound session",
          "[modelregistry]") {
    MockDriver driver;
    ModelRegistry registry;
    auto manifest = makeManifest("acoustic", "com.test.A", "/models/acoustic.onnx");
    auto bound = registry.bind(manifest, &driver);
    REQUIRE(bound.hasValue());

    auto got = registry.getBoundSession("com.test.A", "acoustic");
    REQUIRE(got.hasValue());
    REQUIRE(got.value().get() == bound.value().get());
}

TEST_CASE("ModelRegistry getBoundSession unknown returns FileNotFound",
          "[modelregistry]") {
    ModelRegistry registry;
    auto got = registry.getBoundSession("com.test.A", "nonexistent");
    REQUIRE(!got.hasValue());
}

TEST_CASE("ModelRegistry getBoundSession wrong packageId returns FileNotFound",
          "[modelregistry][bf-31]") {
    MockDriver driver;
    ModelRegistry registry;
    auto manifest = makeManifest("acoustic", "com.test.A", "/models/acoustic.onnx");
    REQUIRE(registry.bind(manifest, &driver).hasValue());

    // Bound for A, queried via B — must fail (no leakage across packages).
    auto got = registry.getBoundSession("com.test.B", "acoustic");
    REQUIRE(!got.hasValue());
}

// ---------------------------------------------------------------------------
// BF-31: Cross-package isolation (same inferenceId, different packageId)
// ---------------------------------------------------------------------------

TEST_CASE("ModelRegistry same inferenceId in different packages creates independent sessions",
          "[modelregistry][bf-31]") {
    MockDriver driver;
    ModelRegistry registry;

    // Two packages both define an inference with id "pitch" but pointing at
    // different model files. Without packageId in the key, the second bind
    // would return the first package's cached session (wrong model loaded).
    auto manifestA = makeManifest("pitch", "com.test.A", "/models/pitch_a.onnx");
    auto manifestB = makeManifest("pitch", "com.test.B", "/models/pitch_b.onnx");

    auto rA = registry.bind(manifestA, &driver);
    auto rB = registry.bind(manifestB, &driver);
    REQUIRE(rA.hasValue());
    REQUIRE(rB.hasValue());

    // Two distinct sessions must have been created.
    REQUIRE(rA.value().get() != rB.value().get());
    REQUIRE(driver.sessionsCreated() >= 2);

    // Each session must have opened its own model file.
    auto *sA = static_cast<MockSession *>(rA.value().get());
    auto *sB = static_cast<MockSession *>(rB.value().get());
    REQUIRE(sA->openedPaths().size() == 1);
    REQUIRE(sB->openedPaths().size() == 1);
    REQUIRE(sA->openedPaths()[0] == "/models/pitch_a.onnx");
    REQUIRE(sB->openedPaths()[0] == "/models/pitch_b.onnx");
}

TEST_CASE("ModelRegistry many packages with same inferenceId create independent sessions",
          "[modelregistry][bf-31]") {
    MockDriver driver;
    ModelRegistry registry;

    const int N = 6;
    for (int i = 0; i < N; ++i) {
        const auto pkg = "com.test.pkg" + std::to_string(i);
        const auto model = "/models/acoustic_" + std::to_string(i) + ".onnx";
        auto manifest = makeManifest("acoustic", pkg, model);
        auto r = registry.bind(manifest, &driver);
        REQUIRE(r.hasValue());
        auto *s = static_cast<MockSession *>(r.value().get());
        REQUIRE(s->openedPaths().size() == 1);
        REQUIRE(s->openedPaths()[0] == model);
    }
    REQUIRE(driver.sessionsCreated() == N);
}

TEST_CASE("ModelRegistry empty packageId does not collide with named packageId",
          "[modelregistry][bf-31]") {
    MockDriver driver;
    ModelRegistry registry;

    auto manifestEmpty = makeManifest("vocoder", "", "/models/vocoder_empty.onnx");
    auto manifestA = makeManifest("vocoder", "com.test.A", "/models/vocoder_a.onnx");

    REQUIRE(registry.bind(manifestEmpty, &driver).hasValue());
    REQUIRE(registry.bind(manifestA, &driver).hasValue());
    REQUIRE(driver.sessionsCreated() == 2);

    // getBoundSession must return different sessions.
    auto gE = registry.getBoundSession("", "vocoder");
    auto gA = registry.getBoundSession("com.test.A", "vocoder");
    REQUIRE(gE.hasValue());
    REQUIRE(gA.hasValue());
    REQUIRE(gE.value().get() != gA.value().get());
}
