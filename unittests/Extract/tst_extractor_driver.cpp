// lib/Extract edge condition test cases (EX-001, EX-003 ~ EX-006)
//
// Covers srt::extract::getInferenceDriver error paths and the success path.
// See the test matrix in docs/refactoring/06-inference-lite-optimization/README.md
// (task T-13). Corresponds to the implementation in
// lib/Extract/src/ExtractorDriver.cpp.
//
// === Test matrix mapping ===
//   EX-001 (P0): nullptr runtime -> ErrorCode::InvalidArgument
//   EX-002:      covered by tst_extractor_no_category.cpp (separate target;
//                see file header there for the rationale)
//   EX-003 (P0): missing "dsdriver" in inference category -> DriverNotFound
//   EX-004 (P1): non-InferenceDriver object registered as "dsdriver" -> SKIP
//                (NO<T>::as<U>() uses std::static_pointer_cast and does not
//                 perform a runtime type check; the `if (!driver)` branch in
//                 ExtractorDriver.cpp:48-53 is dead code, only reachable when
//                 obj is null which is already handled by EX-003. Directly
//                 exercising it would call backend() on a non-InferenceDriver
//                 object, which is UB.)
//   EX-005 (P0): InferenceDriver with backend != "onnx" ->
//                ErrorCode::DriverUnsupportedProvider
//   EX-006 (P1): InferenceDriver with backend == "onnx" -> success
//
// === Mock infrastructure ===
// MockInferenceCategory: a minimal ModuleCategory subclass registered via
//   ModuleCategoryRegistrar<MockInferenceCategory> so every Runtime
//   constructed in this test binary has an "inference" category. The real
//   category (srt::svs::InferenceCategory) is registered by lib/SVS; we
//   do not link srt::svs here (L1, no plugin DLL).
// MockInferenceDriver: a minimal InferenceDriver implementation whose
//   backend() returns a constructor-controlled string, used to exercise
//   EX-005 (non-onnx backend) and EX-006 (onnx backend).
//
// L1 classification: links srt::extract + srt::driver + srt::core; no plugin
// DLL, no ONNX runtime.

#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <synthrt/Extract/ExtractorDriver.h>

using srt::core::ErrorCode;
using srt::core::Expected;
using srt::core::NO;
using srt::core::Runtime;
using srt::driver::InferenceDriver;
using srt::driver::InferenceDriverInitArgs;
using srt::driver::InferenceSession;
using srt::driver::onnx::API_NAME;
using srt::extract::getInferenceDriver;

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
    // category (required for EX-003/EX-005/EX-006 to reach past the
    // "no inference category" guard in getInferenceDriver).
    //
    // The two pure virtual methods (key()/category()) must return the same
    // string used by getInferenceDriver for moduleCategory() lookup.
    class MockInferenceCategory : public srt::core::ModuleCategory {
    public:
        explicit MockInferenceCategory(srt::core::Runtime *rt)
            : srt::core::ModuleCategory("inference", rt) {}

    protected:
        std::string key() const override { return "inference"; }
        std::string category() const override { return "inference"; }
    };

    // Static registrar instance: its constructor registers the factory in
    // Runtime::Impl::moduleCategoryFactories (a process-wide static list).
    // This pollutes every Runtime in this binary, which is exactly what
    // EX-003/EX-005/EX-006 need (and why EX-002 lives in a separate target).
    static srt::core::ModuleCategoryRegistrar<MockInferenceCategory>
        g_mockInferenceCategoryRegistrar;

    // Minimal InferenceDriver mock. backend() is configurable so the same
    // mock exercises both EX-005 (non-onnx) and EX-006 (onnx) paths.
    // Mirrors the pattern in unittests/Driver/tst_driver_edge.cpp.
    class MockInferenceDriver : public InferenceDriver {
    public:
        explicit MockInferenceDriver(std::string backend)
            : m_backend(std::move(backend)) {
            // objectName is informational; the "dsdriver" id used by
            // getInferenceDriver comes from the addObject("dsdriver", ...)
            // call site, not from objectName.
            setObjectName("dsdriver");
        }

        std::string arch() const override { return "mock-arch"; }
        std::string backend() const override { return m_backend; }

        Expected<void> initialize(
            const NO<InferenceDriverInitArgs> & /*args*/) override {
            return {};
        }

        NO<InferenceSession> createSession() override { return nullptr; }

    private:
        std::string m_backend;
    };

} // namespace

// ---------------------------------------------------------------------------
// EX-001: getInferenceDriver with nullptr runtime -> InvalidArgument
//
// First guard in lib/Extract/src/ExtractorDriver.cpp:23-28. The Runtime
// pointer is dereferenced immediately on the next line, so a nullptr must
// be rejected explicitly with InvalidArgument (ROBUST-03).
// ---------------------------------------------------------------------------
TEST_CASE("EX-001: getInferenceDriver with nullptr runtime returns InvalidArgument",
          "[extract][edge]") {
    auto result = getInferenceDriver(nullptr);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error().code() == ErrorCode::InvalidArgument);
    REQUIRE_FALSE(result.error().message().empty());
}

// ---------------------------------------------------------------------------
// EX-003: inference category exists but no "dsdriver" registered -> DriverNotFound
//
// Third guard in lib/Extract/src/ExtractorDriver.cpp:39-45. With a bare
// MockInferenceCategory (no objects added), getFirstObject("dsdriver")
// returns a null NO<NamedObject>, triggering DriverNotFound.
// ---------------------------------------------------------------------------
TEST_CASE("EX-003: getInferenceDriver without dsdriver returns DriverNotFound",
          "[extract][edge]") {
    Runtime runtime;
    // MockInferenceCategory is globally registered, so the "inference"
    // category exists but is empty.
    auto *infCat = runtime.moduleCategory("inference");
    REQUIRE(infCat != nullptr);
    REQUIRE(infCat->getFirstObject("dsdriver") == nullptr);

    auto result = getInferenceDriver(&runtime);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error().code() == ErrorCode::DriverNotFound);
    REQUIRE_FALSE(result.error().message().empty());
}

// ---------------------------------------------------------------------------

// EX-004: non-InferenceDriver object registered as "dsdriver" -> unreachable
//
// NO<T>::as<U>() (include/synthrt/Core/Core/NamedObject.h:100-102) is
// implemented as std::static_pointer_cast<U>, which does NOT perform a
// runtime type check. As a result, when obj holds a non-InferenceDriver
// NamedObject, obj.as<InferenceDriver>() still returns a non-null
// shared_ptr that points to the wrong type.
//
// The `if (!driver)` check in ExtractorDriver.cpp:48-53 is therefore only
// reachable when obj itself is null — which is already handled by EX-003's
// `if (!obj)` guard above. The EX-004 branch is dead code under the current
// NO<T>::as<U>() semantics: directly exercising it would call a virtual
// method through a mistyped pointer (UB). Fixing it would require changing
// NO<T>::as<U>() to use dynamic_pointer_cast (ARCH-02: public API change,
// requires Level bump) — out of scope for T-13. Documented as a design
// observation without a test shell.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// EX-005: InferenceDriver with backend != "onnx" -> DriverUnsupportedProvider
//
// Fifth guard in lib/Extract/src/ExtractorDriver.cpp:56-62. The driver is a
// valid InferenceDriver but its backend() returns a non-"onnx" string, so
// getInferenceDriver rejects it with DriverUnsupportedProvider.
// ---------------------------------------------------------------------------
TEST_CASE("EX-005: getInferenceDriver with non-onnx backend returns DriverUnsupportedProvider",
          "[extract][edge]") {
    Runtime runtime;
    auto *infCat = runtime.moduleCategory("inference");
    REQUIRE(infCat != nullptr);

    // Inject a mock driver whose backend() returns "mock-backend" (!= "onnx").
    auto mockDriver = NO<MockInferenceDriver>::create("mock-backend");
    infCat->addObject("dsdriver", mockDriver);

    auto result = getInferenceDriver(&runtime);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error().code() == ErrorCode::DriverUnsupportedProvider);
    // Error message should mention both the actual and expected backends.
    const auto &msg = result.error().message();
    REQUIRE_FALSE(msg.empty());
    REQUIRE(msg.find("mock-backend") != std::string::npos);
    REQUIRE(msg.find(API_NAME) != std::string::npos);
}

// ---------------------------------------------------------------------------
// EX-006: InferenceDriver with backend == "onnx" -> success
//
// Happy path through lib/Extract/src/ExtractorDriver.cpp:64. The driver is
// a valid InferenceDriver with backend() == srt::driver::onnx::API_NAME
// ("onnx"), so getInferenceDriver returns the driver. The returned NO<>
// must point to the same object that was registered.
// ---------------------------------------------------------------------------
TEST_CASE("EX-006: getInferenceDriver with onnx backend succeeds",
          "[extract][edge]") {
    Runtime runtime;
    auto *infCat = runtime.moduleCategory("inference");
    REQUIRE(infCat != nullptr);

    // Inject a mock driver whose backend() returns "onnx" (== API_NAME).
    auto mockDriver = NO<MockInferenceDriver>::create(API_NAME);
    auto *rawDriver = mockDriver.get();
    infCat->addObject("dsdriver", mockDriver);

    auto result = getInferenceDriver(&runtime);
    REQUIRE(result.hasValue());
    // The returned driver must be the same object we registered.
    REQUIRE(result->get() == rawDriver);
    // Sanity check: the returned driver reports the onnx backend.
    REQUIRE((*result)->backend() == API_NAME);
}
