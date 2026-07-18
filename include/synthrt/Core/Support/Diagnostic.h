#pragma once

#include <string>
#include <vector>

#include <synthrt/Core/srt_core_global.h>

namespace srt::core {

    enum class Severity {
        Info,
        Warning,
        Error,
    };

    /// Error category for grouping error codes by module.
    enum class ErrorCategory {
        None,
        General,    // 0-99
        Package,    // 100-199
        Inference,  // 200-299
        G2P,        // 300-399
        Driver,     // 400-499
        S2P,        // 500-599
        SVS,        // 600-699
        Audio,      // 700-799
        Extract,    // 800-899
    };

    /// Hierarchical error code. Values are grouped by module in 100-code segments.
    /// New codes can only be appended within each segment, never reordered (ARCH-02).
    enum class ErrorCode {
        // === General (0-99) ===
        None = 0,
        InvalidFormat,
        FileNotFound,
        FileNotOpen,
        FileDuplicated,
        RecursiveDependency,
        FeatureNotSupported,
        InvalidArgument,
        NotImplemented,
        SessionError,
        Timeout,
        Aborted,
        OutOfMemory,
        Unknown,

        // === Package (100-199) ===
        PackageRootInvalid = 100,
        PackageSourceAfterInitialize,
        PackageScanAfterInitialize,
        PackageManifestInvalid,
        PackageManifestMissingField,
        PackageManifestNotFound,
        PackageDependencyMissing,
        PackageDependencyCycle,
        PackageVersionConflict,
        PackageSingerConfigInvalid,
        PackageSingerConfigMissing,
        PackageSingerIdEmpty,
        PackageDuplicate,

        // === Inference (200-299) ===
        InferenceNotInitialized = 200,
        InferenceAlreadyRunning,
        InferenceModelNotFound,
        InferenceModelLoadFailed,
        InferenceModelInitFailed,
        InferenceStartFailed,
        InferenceRunFailed,
        InferenceStageMissing,
        InferenceStageSpecNull,
        InferenceSpeakerNotFound,
        InferenceInputInvalid,
        InferenceTensorCreateFailed,
        InferenceOutputEmpty,
        InferenceDataTypeMismatch,
        InferenceSampleRateMismatch,
        ModelBusy,
        StaleModelSet,
        // V3-09: ensure* API failure codes (values verified to avoid collisions
        // with the existing Inference segment ending at StaleModelSet=216).
        LoadFailed,                ///< 217 — ONNX session creation / load failure
        RuntimePackageNotLoaded,  ///< 218 — Runtime::loadPackage not called for singer's package

        // === G2P (300-399) ===
        G2pSuccess = 300,
        G2pConfigError,
        G2pFileSystemError,
        G2pDependencyError,
        G2pRuntimeError,
        G2pNotImplementedError,
        G2pInitializationError,
        G2pValidationError,
        G2pNullPointerError,
        G2pIndexError,
        G2pTimeoutError,
        G2pAlreadyInitialized,
        G2pRouteNotFound,
        G2pPackageNotFound,
        G2pPluginNotFound,
        G2pDriverNotFound,
        G2pDriverInitFailed,
        G2pConversionFailed,
        G2pSessionError,
        G2pContextNotFound,
        G2pTaskNotFound,
        // V3-09 / V3-10: multi-version routing ambiguity (value verified to
        // avoid collision with G2pTaskNotFound=320).
        G2pVersionAmbiguous,      ///< 321 — caller omitted version while packageId has multiple versions

        // === Driver (400-499) ===
        DriverNotFound = 400,
        DriverInitFailed,
        DriverSessionCreateFailed,
        DriverSessionRunFailed,
        DriverUnsupportedProvider,
        DriverPluginNotFound,

        // === S2P (500-599) ===
        S2pResourceNotFound = 500,
        S2pConversionFailed,
        S2pScriptError,
        S2pDictionaryError,

        // === SVS (600-699) ===
        SvsSingerNotFound = 600,
        SvsSingerNotLoaded,
        SvsStageResolveFailed,
        SvsCategoryNotFound,
        // V3-21: multi-version singer ambiguity — mirrors G2pVersionAmbiguous
        // for the SVS layer. Used when a singerId matches multiple loaded
        // singers and the caller omitted packageId/version to disambiguate.
        SvsSingerAmbiguous,

        // === Audio (700-799) ===
        AudioDecodeFailed = 700,
        AudioResampleFailed,
        AudioUnsupportedFormat,
        AudioInvalidBuffer,
        AudioWriteFailed,

        // === Extract (800-899) ===
        ExtractNotInitialized = 800,
        ExtractModelOpenFailed,
        ExtractInferenceFailed,
        ExtractOutputInvalid,
        ExtractPluginNotFound,
        ExtractUnsupportedVersion,
    };

    /// Returns the category for a given error code.
    SRT_CORE_EXPORT ErrorCategory errorCodeCategory(ErrorCode code) noexcept;

    /// Returns a human-readable string for a given error code.
    /// Format: "Inference::ModelLoadFailed", "Package::ManifestInvalid", etc.
    SRT_CORE_EXPORT const char *errorCodeToString(ErrorCode code) noexcept;

    /// Returns a human-readable string for a given error category.
    SRT_CORE_EXPORT const char *errorCategoryToString(ErrorCategory category) noexcept;

    struct SRT_CORE_EXPORT Diagnostic {
        ErrorCode code = ErrorCode::None;
        Severity severity = Severity::Error;
        std::string message;
        std::string location;       // "file:line:function" from source_location
        std::string packageId;
        std::string singerId;
        std::string language;
        std::string moduleId;
        std::string providerKey;
        std::vector<std::string> trace;
    };

}
