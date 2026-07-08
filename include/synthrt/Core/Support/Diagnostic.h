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

    enum class ErrorCode {
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
        PackageRootInvalid,
        PackageSourceAfterInitialize,
        PackageScanAfterInitialize,
        PackageManifestInvalid,
        PackageManifestMissingField,
    };

    struct SRT_CORE_EXPORT Diagnostic {
        ErrorCode code = ErrorCode::None;
        Severity severity = Severity::Error;
        std::string message;
        std::string location;
        std::string packageId;
        std::string singerId;
        std::string language;
        std::string moduleId;
        std::string providerKey;
        std::vector<std::string> trace;
    };

}
