// lib/Extract edge case: EX-002 (no inference category)
//
// This test file is compiled into a SEPARATE target from tst_extractor_driver.cpp
// because it must NOT register a MockInferenceCategory via the global
// ModuleCategoryRegistrar. The registrar injects a factory into a process-wide
// static list (Runtime::Impl::moduleCategoryFactories) which is consumed by
// every Runtime constructor; once registered, every Runtime instance would
// have an "inference" category, making EX-002 ("runtime without inference
// category") unreachable.
//
// EX-002 verifies the second error branch in getInferenceDriver:
//   auto *cate = runtime->moduleCategory("inference");
//   if (!cate) {
//       return Error(ErrorCode::ExtractNotInitialized, ...);
//   }
// (lib/Extract/src/ExtractorDriver.cpp:30-37)
//
// L1 classification: links srt::extract + srt::core only; no plugin DLL, no
// ONNX runtime. The default-constructed Runtime has no module categories
// registered (no SRT_CORE_DEFINE_MODULE_CATEGORY translation unit linked),
// so moduleCategory("inference") returns nullptr.

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Extract/ExtractorDriver.h>

using srt::core::ErrorCode;
using srt::core::Expected;
using srt::core::NO;
using srt::core::Runtime;
using srt::driver::InferenceDriver;
using srt::extract::getInferenceDriver;

namespace srt::core {
// Catch2 needs operator<< to stream ErrorCode values in failed assertions.
inline std::ostream &operator<<(std::ostream &os, const ErrorCode &code) {
    return os << errorCodeToString(code);
}
} // namespace srt::core

// ---------------------------------------------------------------------------
// EX-002: Runtime without "inference" module category -> ExtractNotInitialized
//
// A default-constructed Runtime has no module categories registered (the
// "inference" category is normally registered by srt::svs::InferenceCategory
// via SRT_CORE_DEFINE_MODULE_CATEGORY in lib/SVS/InferenceContrib.cpp; this
// test target does not link srt::svs). getInferenceDriver must therefore
// fail with ErrorCode::ExtractNotInitialized, matching the second error
// branch in lib/Extract/src/ExtractorDriver.cpp:30-37.
//
// Note: the error message mentions "runtime is not initialized with an
// inference module" — this refers to the missing module category, not the
// Runtime::initialize() two-phase init. The Runtime need not be initialized
// for this branch to trigger.
// ---------------------------------------------------------------------------
TEST_CASE("EX-002: getInferenceDriver without inference category returns ExtractNotInitialized",
          "[extract][edge]") {
    Runtime runtime;
    // Sanity check: no "inference" category is registered on a bare Runtime.
    REQUIRE(runtime.moduleCategory("inference") == nullptr);

    auto result = getInferenceDriver(&runtime);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error().code() == ErrorCode::ExtractNotInitialized);
    REQUIRE_FALSE(result.error().message().empty());
}
