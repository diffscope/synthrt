// Unit tests for the refactored srt::core error system.
//
// Covers ErrorCode category mapping, errorCodeToString, Error constructors
// and toString, factory functions, appendTrace, and Expected convenience
// methods.

#include <source_location>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

using namespace srt::core;

// ---------------------------------------------------------------------------
// a. ErrorCode category mapping (errorCodeCategory)
// ---------------------------------------------------------------------------
TEST_CASE("errorCodeCategory maps codes to categories", "[error][category]") {
    SECTION("None maps to None") {
        REQUIRE(errorCodeCategory(ErrorCode::None) == ErrorCategory::None);
    }
    SECTION("General codes (0-99) map to General") {
        REQUIRE(errorCodeCategory(ErrorCode::InvalidFormat) == ErrorCategory::General);
        REQUIRE(errorCodeCategory(ErrorCode::FileNotFound) == ErrorCategory::General);
        REQUIRE(errorCodeCategory(ErrorCode::InvalidArgument) == ErrorCategory::General);
        REQUIRE(errorCodeCategory(ErrorCode::SessionError) == ErrorCategory::General);
        REQUIRE(errorCodeCategory(ErrorCode::Unknown) == ErrorCategory::General);
    }
    SECTION("Package codes (100-199) map to Package") {
        REQUIRE(errorCodeCategory(ErrorCode::PackageRootInvalid) == ErrorCategory::Package);
        REQUIRE(errorCodeCategory(ErrorCode::PackageManifestInvalid) == ErrorCategory::Package);
        REQUIRE(errorCodeCategory(ErrorCode::PackageDuplicate) == ErrorCategory::Package);
    }
    SECTION("Inference codes (200-299) map to Inference") {
        REQUIRE(errorCodeCategory(ErrorCode::InferenceNotInitialized) == ErrorCategory::Inference);
        REQUIRE(errorCodeCategory(ErrorCode::InferenceModelLoadFailed) == ErrorCategory::Inference);
        REQUIRE(errorCodeCategory(ErrorCode::InferenceSampleRateMismatch) == ErrorCategory::Inference);
    }
    SECTION("G2P codes (300-399) map to G2P") {
        REQUIRE(errorCodeCategory(ErrorCode::G2pSuccess) == ErrorCategory::G2P);
        REQUIRE(errorCodeCategory(ErrorCode::G2pDependencyError) == ErrorCategory::G2P);
        REQUIRE(errorCodeCategory(ErrorCode::G2pTaskNotFound) == ErrorCategory::G2P);
    }
    SECTION("Driver/S2P/SVS codes map to their categories") {
        REQUIRE(errorCodeCategory(ErrorCode::DriverNotFound) == ErrorCategory::Driver);
        REQUIRE(errorCodeCategory(ErrorCode::S2pResourceNotFound) == ErrorCategory::S2P);
        REQUIRE(errorCodeCategory(ErrorCode::SvsSingerNotFound) == ErrorCategory::SVS);
    }
}

// ---------------------------------------------------------------------------
// b. errorCodeToString / errorCategoryToString
// ---------------------------------------------------------------------------
TEST_CASE("errorCodeToString returns human-readable names", "[error][string]") {
    REQUIRE(std::string(errorCodeToString(ErrorCode::None)) == "None");
    REQUIRE(std::string(errorCodeToString(ErrorCode::FileNotFound)) == "FileNotFound");
    REQUIRE(std::string(errorCodeToString(ErrorCode::InferenceModelLoadFailed)) ==
            "Inference::ModelLoadFailed");
    REQUIRE(std::string(errorCodeToString(ErrorCode::G2pDependencyError)) == "G2P::DependencyError");
    REQUIRE(std::string(errorCodeToString(ErrorCode::PackageManifestInvalid)) ==
            "Package::ManifestInvalid");
    REQUIRE(std::string(errorCodeToString(ErrorCode::SvsSingerNotFound)) == "SVS::SingerNotFound");
}

TEST_CASE("errorCategoryToString returns human-readable names", "[error][string]") {
    REQUIRE(std::string(errorCategoryToString(ErrorCategory::None)) == "None");
    REQUIRE(std::string(errorCategoryToString(ErrorCategory::General)) == "General");
    REQUIRE(std::string(errorCategoryToString(ErrorCategory::Package)) == "Package");
    REQUIRE(std::string(errorCategoryToString(ErrorCategory::Inference)) == "Inference");
    REQUIRE(std::string(errorCategoryToString(ErrorCategory::G2P)) == "G2P");
}

// ---------------------------------------------------------------------------
// c. Error constructors and toString
// ---------------------------------------------------------------------------
TEST_CASE("Error constructs with ErrorCode and message", "[error][ctor]") {
    Error e(ErrorCode::FileNotFound, "test");
    REQUIRE(!e.ok());
    REQUIRE(e.code() == ErrorCode::FileNotFound);
    REQUIRE(e.category() == ErrorCategory::General);
    REQUIRE(e.message() == "test");
    REQUIRE(e.toString().find("[FileNotFound]") != std::string::npos);
    REQUIRE(e.toString().find("test") != std::string::npos);
}

TEST_CASE("Error with None code is ok and toString is empty", "[error][ctor]") {
    Error e(ErrorCode::None, "");
    REQUIRE(e.ok());
    REQUIRE(e.code() == ErrorCode::None);
    REQUIRE(e.category() == ErrorCategory::None);
    REQUIRE(e.toString().empty());
}

TEST_CASE("Error captures source location automatically", "[error][ctor]") {
    Error e(ErrorCode::InvalidArgument, "bad arg");
    REQUIRE(!e.sourceLocation().empty());
    // sourceLocation format: "file:line:function"
    REQUIRE(e.sourceLocation().find(':') != std::string::npos);
    // toString includes the "at <location>" line
    REQUIRE(e.toString().find("at ") != std::string::npos);
}

TEST_CASE("Error legacy Type constructor is backward compatible", "[error][legacy]") {
    Error e(Error::SessionError, "session failed");
    REQUIRE(!e.ok());
    REQUIRE(e.type() == Error::SessionError);
    REQUIRE(e.code() == ErrorCode::SessionError);
    REQUIRE(e.message() == "session failed");
}

TEST_CASE("Error legacy Type NoError constructs as ok", "[error][legacy]") {
    Error e(Error::Type::NoError);
    REQUIRE(e.ok());
    REQUIRE(e.type() == Error::NoError);
    REQUIRE(e.code() == ErrorCode::None);
}

// ---------------------------------------------------------------------------
// d. Factory functions
// ---------------------------------------------------------------------------
TEST_CASE("Error::inferenceError populates singerId and moduleId", "[error][factory]") {
    auto e = Error::inferenceError(ErrorCode::InferenceModelLoadFailed, "load failed", "singer-1",
                                   "acoustic");
    REQUIRE(e.code() == ErrorCode::InferenceModelLoadFailed);
    REQUIRE(e.category() == ErrorCategory::Inference);
    REQUIRE(e.diagnostic().singerId == "singer-1");
    REQUIRE(e.diagnostic().moduleId == "acoustic");
    REQUIRE(e.toString().find("singerId:") != std::string::npos);
    REQUIRE(e.toString().find("moduleId:") != std::string::npos);
}

TEST_CASE("Error::packageError populates packageId", "[error][factory]") {
    auto e = Error::packageError(ErrorCode::PackageManifestInvalid, "bad manifest", "pkg-1");
    REQUIRE(e.code() == ErrorCode::PackageManifestInvalid);
    REQUIRE(e.category() == ErrorCategory::Package);
    REQUIRE(e.diagnostic().packageId == "pkg-1");
    REQUIRE(e.toString().find("packageId:") != std::string::npos);
}

TEST_CASE("Error::g2pError populates language and packageId", "[error][factory]") {
    auto e = Error::g2pError(ErrorCode::G2pDependencyError, "missing dep", "en", "pkg-2");
    REQUIRE(e.code() == ErrorCode::G2pDependencyError);
    REQUIRE(e.category() == ErrorCategory::G2P);
    REQUIRE(e.diagnostic().language == "en");
    REQUIRE(e.diagnostic().packageId == "pkg-2");
    REQUIRE(e.toString().find("language:") != std::string::npos);
    REQUIRE(e.toString().find("packageId:") != std::string::npos);
}

// ---------------------------------------------------------------------------
// e. appendTrace
// ---------------------------------------------------------------------------
TEST_CASE("Error::appendTrace records trace entries", "[error][trace]") {
    SECTION("appendTrace(string) adds one entry") {
        Error e(ErrorCode::InvalidArgument, "err");
        REQUIRE(e.diagnostic().trace.empty());

        e.appendTrace(std::string("layer-1"));

        REQUIRE(e.diagnostic().trace.size() == 1);
        REQUIRE(e.diagnostic().trace[0] == "layer-1");
        REQUIRE(e.toString().find("trace:") != std::string::npos);
        REQUIRE(e.toString().find("layer-1") != std::string::npos);
    }
    SECTION("appendTrace(source_location, note) adds entry with note") {
        Error e(ErrorCode::InvalidArgument, "err");
        e.appendTrace(std::source_location::current(), "upper layer");

        REQUIRE(e.diagnostic().trace.size() == 1);
        REQUIRE(e.diagnostic().trace[0].find("upper layer") != std::string::npos);
        // Entry also embeds the source location "file:line:function"
        REQUIRE(e.diagnostic().trace[0].find(':') != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// f. Expected convenience methods (Expected<T> only)
// ---------------------------------------------------------------------------
TEST_CASE("Expected convenience methods on success value", "[expected]") {
    Expected<int> ex(42);
    REQUIRE(ex.hasValue());
    REQUIRE(ex.errorMessage().empty());
    REQUIRE(ex.errorCode() == ErrorCode::None);
    REQUIRE(ex.errorCategory() == ErrorCategory::None);
    REQUIRE(!ex.isError(ErrorCode::FileNotFound));
    REQUIRE(ex.errorString().empty());
}

TEST_CASE("Expected convenience methods on error", "[expected]") {
    Expected<int> ex{Error(ErrorCode::FileNotFound, "missing file")};
    REQUIRE(!ex.hasValue());
    REQUIRE(!ex.errorMessage().empty());
    REQUIRE(ex.errorMessage() == "missing file");
    REQUIRE(ex.errorCode() == ErrorCode::FileNotFound);
    REQUIRE(ex.errorCategory() == ErrorCategory::General);
    REQUIRE(ex.isError(ErrorCode::FileNotFound));
    REQUIRE(!ex.isError(ErrorCode::InvalidArgument));
}

TEST_CASE("Expected errorString contains category and code", "[expected]") {
    Expected<int> ex{Error(ErrorCode::InferenceModelLoadFailed, "load failed")};
    auto s = ex.errorString();
    REQUIRE(!s.empty());
    REQUIRE(s.find("[Inference::ModelLoadFailed]") != std::string::npos);
    REQUIRE(s.find("load failed") != std::string::npos);
}
