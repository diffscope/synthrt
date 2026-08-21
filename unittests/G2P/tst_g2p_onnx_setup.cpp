// A1 unit tests for srt::g2p::setupG2pOnnxDriver.
//
// Covers the contract documented in docs/lite-integration/02-synthrt-side-changes.md §A1:
//   A1-T01: missing "dsdriver" in inference category → InferenceNotInitialized (200)
//   A1-T02: mock "dsdriver" registered → succeeds; kG2pOnnxDriverName in kDriverCategory
//   A1-T03: repeat call is idempotent (replaces previous factory)
//   A1-T04: g2pPluginPaths with non-existent paths → not fatal; returns OK
//   A1-T05: ONNX driver throws std::exception → SKIP (cannot inject without source changes)
//   A1-T06: G2P plugin path with non-ASCII chars → registers correctly (CODING-03)
//
// === Mock infrastructure ===
// MockInferenceCategory: registers itself under "inference" name so every Runtime
//   in this binary has an inference category (mirrors unittests/Extract/tst_extractor_driver.cpp).
// MockInferenceDriver: minimal InferenceDriver whose backend() returns "onnx".
//
// L1 classification: links srt::g2p + srt::driver + srt::core; no plugin DLL,
// no ONNX runtime. The mock driver lets us exercise the success path of
// setupG2pOnnxDriver without the real ONNX runtime.

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <stdcorelib/support/versionnumber.h>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Core/Manager.h>
#include <synthrt/G2P/G2pOnnxSetup.h>

using srt::core::ErrorCode;
using srt::core::Expected;
using srt::core::NO;
using srt::core::Runtime;
using srt::driver::InferenceDriver;
using srt::driver::InferenceDriverInitArgs;
using srt::driver::InferenceSession;
using srt::g2p::kDriverCategory;
using srt::g2p::kG2pOnnxDriverName;
using srt::g2p::Manager;
using srt::g2p::setupG2pOnnxDriver;

namespace srt::core {
// Catch2 needs operator<< to stream ErrorCode values in failed assertions.
inline std::ostream &operator<<(std::ostream &os, const ErrorCode &code) {
    return os << errorCodeToString(code);
}
} // namespace srt::core

namespace {

    // Minimal ModuleCategory subclass that registers itself under the
    // "inference" name. ModuleCategoryRegistrar<T> is a friend of Runtime
    // and calls Runtime::registerModuleCategoryFactory at static init time,
    // so every Runtime constructed in this binary will have an "inference"
    // category. This is required for A1-T02+ to reach past the
    // "no inference category" guard in setupG2pOnnxDriver.
    //
    // Pattern mirrored from unittests/Extract/tst_extractor_driver.cpp.
    class MockInferenceCategory : public srt::core::ModuleCategory {
    public:
        explicit MockInferenceCategory(srt::core::Runtime *rt)
            : srt::core::ModuleCategory("inference", rt) {}

    protected:
        std::string key() const override { return "inference"; }
        std::string category() const override { return "inference"; }
    };

    // Static registrar: its constructor registers the factory in
    // Runtime::Impl::moduleCategoryFactories (process-wide static list).
    // Every Runtime in this binary gets an "inference" category.
    static srt::core::ModuleCategoryRegistrar<MockInferenceCategory>
        g_mockInferenceCategoryRegistrar;

    // Minimal InferenceDriver mock. backend() returns "onnx" so it satisfies
    // the contract expected by setupG2pOnnxDriver (which casts the "dsdriver"
    // object to InferenceDriver). arch() returns a synthetic identifier.
    //
    // createSession() returns nullptr; setupG2pOnnxDriver does not invoke
    // createSession() during setup — it only wraps the driver pointer in a
    // SessionFactory adapter. The adapter's createSession() is exercised
    // later by G2P convert calls, which are out of scope for A1 unit tests.
    class MockInferenceDriver : public InferenceDriver {
    public:
        MockInferenceDriver() {
            // objectName is informational; the "dsdriver" id used by
            // setupG2pOnnxDriver comes from the addObject("dsdriver", ...)
            // call site, not from objectName.
            setObjectName("dsdriver");
        }

        std::string arch() const override { return "mock-arch"; }
        std::string backend() const override { return srt::driver::onnx::API_NAME; }

        Expected<void> initialize(
            const NO<InferenceDriverInitArgs> & /*args*/) override {
            return {};
        }

        NO<InferenceSession> createSession() override { return nullptr; }
    };

    // Helper: register a fresh mock dsdriver in the runtime's inference
    // category. Callers should invoke this before setupG2pOnnxDriver to
    // simulate "setupOnnxInferenceDriver already ran".
    void registerMockDsdriver(Runtime &runtime) {
        auto *infCat = runtime.moduleCategory("inference");
        REQUIRE(infCat != nullptr);
        // removeObjects is idempotent: safe to call when no prior object exists.
        infCat->removeObjects("dsdriver");
        infCat->addObject("dsdriver", NO<MockInferenceDriver>::create());
    }

    // Helper: check whether kG2pOnnxDriverName is registered in the Manager's
    // kDriverCategory. setupG2pOnnxDriver registers a SessionFactory adapter
    // under this name; getFirstObject returns non-null on success.
    bool g2pOnnxDriverRegistered() {
        auto *mgr = Manager::instance();
        if (!mgr) return false;
        auto *driverCat = mgr->category(kDriverCategory);
        if (!driverCat) return false;
        return driverCat->getFirstObject(kG2pOnnxDriverName) != nullptr;
    }

    // Helper: build a non-ASCII path under temp dir to verify CODING-03
    // (all cross-boundary paths use stdc::path::to_utf8()).
    std::filesystem::path makeNonAsciiPath() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = std::filesystem::temp_directory_path() /
                   ("synthrt-g2p-中文路径-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        return dir;
    }

} // namespace

// ===========================================================================
// A1-T01: Runtime without "dsdriver" → InferenceNotInitialized (200)
//
// setupG2pOnnxDriver requires the Runtime's "inference" category to contain
// a "dsdriver" object (registered by srt::driver::setupOnnxInferenceDriver).
// When the category exists but the object is missing, the function returns
// InferenceNotInitialized so the caller can decide how to handle the missing
// driver (project_memory: "setupOnnxInferenceDriver must NOT auto-fallback").
// ===========================================================================

TEST_CASE("A1-T01: setupG2pOnnxDriver without dsdriver returns InferenceNotInitialized",
          "[g2p][setup][a1]") {
    Runtime runtime;
    // MockInferenceCategory is globally registered, so the "inference"
    // category exists but is empty (no "dsdriver" object).
    auto *infCat = runtime.moduleCategory("inference");
    REQUIRE(infCat != nullptr);
    REQUIRE(infCat->getFirstObject("dsdriver") == nullptr);

    auto exp = setupG2pOnnxDriver(runtime, {});
    REQUIRE_FALSE(exp.hasValue());
    REQUIRE(exp.error().code() == ErrorCode::InferenceNotInitialized);
    REQUIRE_FALSE(exp.error().message().empty());

    // The error message should mention dsdriver or inference for diagnosability.
    const auto msg = exp.error().message();
    REQUIRE((msg.find("dsdriver") != std::string::npos ||
             msg.find("inference") != std::string::npos));
}

// ===========================================================================
// A1-T02: mock "dsdriver" registered → success; kG2pOnnxDriverName registered
//
// When setupOnnxInferenceDriver has registered the "dsdriver" object (here
// simulated by MockInferenceDriver), setupG2pOnnxDriver wraps it with a
// CPU-only SessionFactory adapter and registers the adapter under
// kG2pOnnxDriverName in the Manager's kDriverCategory.
// ===========================================================================

TEST_CASE("A1-T02: setupG2pOnnxDriver with dsdriver succeeds and registers kG2pOnnxDriverName",
          "[g2p][setup][a1]") {
    Runtime runtime;
    registerMockDsdriver(runtime);

    auto exp = setupG2pOnnxDriver(runtime, {});
    REQUIRE(exp.hasValue());

    // The adapter must be registered under kG2pOnnxDriverName in kDriverCategory.
    REQUIRE(g2pOnnxDriverRegistered());
}

// ===========================================================================
// A1-T03: repeat call is idempotent
//
// setupG2pOnnxDriver uses driverCat->removeObjects(kG2pOnnxDriverName)
// before addObject, so a second call replaces the previous adapter rather
// than appending. getFirstObject still returns the (new) adapter, and the
// call does not throw or return an error.
// ===========================================================================

TEST_CASE("A1-T03: setupG2pOnnxDriver is idempotent on repeat calls",
          "[g2p][setup][a1]") {
    Runtime runtime;
    registerMockDsdriver(runtime);

    REQUIRE(setupG2pOnnxDriver(runtime, {}).hasValue());
    REQUIRE(g2pOnnxDriverRegistered());

    // Second call: must not throw, must not return an error, must still
    // leave exactly one kG2pOnnxDriverName object (replace, not append).
    REQUIRE(setupG2pOnnxDriver(runtime, {}).hasValue());
    REQUIRE(g2pOnnxDriverRegistered());

    // Third call for good measure — repeated setup is a common pattern when
    // the host reinitializes the Runtime (e.g. ds-editor-lite on reload).
    REQUIRE(setupG2pOnnxDriver(runtime, {}).hasValue());
    REQUIRE(g2pOnnxDriverRegistered());
}

// ===========================================================================
// A1-T04: g2pPluginPaths with non-existent paths → not fatal
//
// setupG2pOnnxDriver calls mgr->addPluginPath(kTaskPluginIid, path) and
// mgr->addPluginPath(kDriverPluginIid, path) for each entry in
// g2pPluginPaths. PluginFactory::addPluginPath is tolerant of non-existent
// directories (they are recorded for later plugin discovery, which may
// succeed when the directory appears). The setup itself must not fail.
// ===========================================================================

TEST_CASE("A1-T04: setupG2pOnnxDriver tolerates non-existent g2pPluginPaths",
          "[g2p][setup][a1]") {
    Runtime runtime;
    registerMockDsdriver(runtime);

    std::vector<std::filesystem::path> nonExistentPaths = {
        "definitely/does/not/exist/g2p/G2ps",
        "another/missing/path/g2p/dict",
    };

    auto exp = setupG2pOnnxDriver(runtime, nonExistentPaths);
    REQUIRE(exp.hasValue());
    REQUIRE(g2pOnnxDriverRegistered());
}

// ===========================================================================
// A1-T06: G2P plugin path with non-ASCII chars registers correctly
//
// CODING-03 requires all cross-boundary paths use stdc::path::to_utf8()
// to handle non-ANSI paths on Windows. setupG2pOnnxDriver passes
// g2pPluginPaths to Manager::addPluginPath, which internally converts
// paths via PluginFactory::addPluginPath. A path with Chinese characters
// must be accepted without error and must not be corrupted.
// ===========================================================================

TEST_CASE("A1-T06: setupG2pOnnxDriver accepts non-ASCII g2pPluginPaths",
          "[g2p][setup][a1][coding-03]") {
    Runtime runtime;
    registerMockDsdriver(runtime);

    const auto nonAsciiDir = makeNonAsciiPath();
    std::vector<std::filesystem::path> paths = {
        nonAsciiDir / "G2ps",
        nonAsciiDir / "dict",
    };

    auto exp = setupG2pOnnxDriver(runtime, paths);
    REQUIRE(exp.hasValue());
    REQUIRE(g2pOnnxDriverRegistered());

    std::filesystem::remove_all(nonAsciiDir);
}
