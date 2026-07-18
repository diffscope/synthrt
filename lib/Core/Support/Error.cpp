#include "Error.h"

#include <filesystem>
#include <format>

namespace srt::core {

    // === errorCodeCategory ===
    ErrorCategory errorCodeCategory(ErrorCode code) noexcept {
        int v = static_cast<int>(code);
        if (v == 0)
            return ErrorCategory::None;
        if (v >= 800)
            return ErrorCategory::Extract;
        if (v >= 700)
            return ErrorCategory::Audio;
        if (v >= 600)
            return ErrorCategory::SVS;
        if (v >= 500)
            return ErrorCategory::S2P;
        if (v >= 400)
            return ErrorCategory::Driver;
        if (v >= 300)
            return ErrorCategory::G2P;
        if (v >= 200)
            return ErrorCategory::Inference;
        if (v >= 100)
            return ErrorCategory::Package;
        return ErrorCategory::General;
    }

    // === errorCodeToString ===
    const char *errorCodeToString(ErrorCode code) noexcept {
        switch (code) {
            // General (0-99)
            case ErrorCode::None:                   return "None";
            case ErrorCode::InvalidFormat:          return "InvalidFormat";
            case ErrorCode::FileNotFound:           return "FileNotFound";
            case ErrorCode::FileNotOpen:            return "FileNotOpen";
            case ErrorCode::FileDuplicated:         return "FileDuplicated";
            case ErrorCode::RecursiveDependency:    return "RecursiveDependency";
            case ErrorCode::FeatureNotSupported:    return "FeatureNotSupported";
            case ErrorCode::InvalidArgument:        return "InvalidArgument";
            case ErrorCode::NotImplemented:         return "NotImplemented";
            case ErrorCode::SessionError:           return "SessionError";
            case ErrorCode::Timeout:                return "Timeout";
            case ErrorCode::Aborted:                return "Aborted";
            case ErrorCode::OutOfMemory:            return "OutOfMemory";
            case ErrorCode::Unknown:                return "Unknown";

            // Package (100-199)
            case ErrorCode::PackageRootInvalid:           return "Package::RootInvalid";
            case ErrorCode::PackageSourceAfterInitialize: return "Package::SourceAfterInitialize";
            case ErrorCode::PackageScanAfterInitialize:   return "Package::ScanAfterInitialize";
            case ErrorCode::PackageManifestInvalid:       return "Package::ManifestInvalid";
            case ErrorCode::PackageManifestMissingField:  return "Package::ManifestMissingField";
            case ErrorCode::PackageManifestNotFound:      return "Package::ManifestNotFound";
            case ErrorCode::PackageDependencyMissing:     return "Package::DependencyMissing";
            case ErrorCode::PackageDependencyCycle:       return "Package::DependencyCycle";
            case ErrorCode::PackageVersionConflict:       return "Package::VersionConflict";
            case ErrorCode::PackageSingerConfigInvalid:   return "Package::SingerConfigInvalid";
            case ErrorCode::PackageSingerConfigMissing:   return "Package::SingerConfigMissing";
            case ErrorCode::PackageSingerIdEmpty:         return "Package::SingerIdEmpty";
            case ErrorCode::PackageDuplicate:             return "Package::Duplicate";

            // Inference (200-299)
            case ErrorCode::InferenceNotInitialized:      return "Inference::NotInitialized";
            case ErrorCode::InferenceAlreadyRunning:      return "Inference::AlreadyRunning";
            case ErrorCode::InferenceModelNotFound:       return "Inference::ModelNotFound";
            case ErrorCode::InferenceModelLoadFailed:     return "Inference::ModelLoadFailed";
            case ErrorCode::InferenceModelInitFailed:     return "Inference::ModelInitFailed";
            case ErrorCode::InferenceStartFailed:         return "Inference::StartFailed";
            case ErrorCode::InferenceRunFailed:           return "Inference::RunFailed";
            case ErrorCode::InferenceStageMissing:        return "Inference::StageMissing";
            case ErrorCode::InferenceStageSpecNull:       return "Inference::StageSpecNull";
            case ErrorCode::InferenceSpeakerNotFound:     return "Inference::SpeakerNotFound";
            case ErrorCode::InferenceInputInvalid:        return "Inference::InputInvalid";
            case ErrorCode::InferenceTensorCreateFailed:  return "Inference::TensorCreateFailed";
            case ErrorCode::InferenceOutputEmpty:         return "Inference::OutputEmpty";
            case ErrorCode::InferenceDataTypeMismatch:   return "Inference::DataTypeMismatch";
            case ErrorCode::InferenceSampleRateMismatch: return "Inference::SampleRateMismatch";
            case ErrorCode::ModelBusy:                   return "Inference::ModelBusy";
            case ErrorCode::StaleModelSet:               return "Inference::StaleModelSet";

            // G2P (300-399)
            case ErrorCode::G2pSuccess:              return "G2P::Success";
            case ErrorCode::G2pConfigError:          return "G2P::ConfigError";
            case ErrorCode::G2pFileSystemError:      return "G2P::FileSystemError";
            case ErrorCode::G2pDependencyError:      return "G2P::DependencyError";
            case ErrorCode::G2pRuntimeError:         return "G2P::RuntimeError";
            case ErrorCode::G2pNotImplementedError:  return "G2P::NotImplementedError";
            case ErrorCode::G2pInitializationError:  return "G2P::InitializationError";
            case ErrorCode::G2pValidationError:      return "G2P::ValidationError";
            case ErrorCode::G2pNullPointerError:     return "G2P::NullPointerError";
            case ErrorCode::G2pIndexError:           return "G2P::IndexError";
            case ErrorCode::G2pTimeoutError:         return "G2P::TimeoutError";
            case ErrorCode::G2pAlreadyInitialized:   return "G2P::AlreadyInitialized";
            case ErrorCode::G2pRouteNotFound:        return "G2P::RouteNotFound";
            case ErrorCode::G2pPackageNotFound:      return "G2P::PackageNotFound";
            case ErrorCode::G2pPluginNotFound:       return "G2P::PluginNotFound";
            case ErrorCode::G2pDriverNotFound:       return "G2P::DriverNotFound";
            case ErrorCode::G2pDriverInitFailed:     return "G2P::DriverInitFailed";
            case ErrorCode::G2pConversionFailed:     return "G2P::ConversionFailed";
            case ErrorCode::G2pSessionError:         return "G2P::SessionError";
            case ErrorCode::G2pContextNotFound:      return "G2P::ContextNotFound";
            case ErrorCode::G2pTaskNotFound:         return "G2P::TaskNotFound";

            // Driver (400-499)
            case ErrorCode::DriverNotFound:             return "Driver::NotFound";
            case ErrorCode::DriverInitFailed:           return "Driver::InitFailed";
            case ErrorCode::DriverSessionCreateFailed:  return "Driver::SessionCreateFailed";
            case ErrorCode::DriverSessionRunFailed:     return "Driver::SessionRunFailed";
            case ErrorCode::DriverUnsupportedProvider:  return "Driver::UnsupportedProvider";
            case ErrorCode::DriverPluginNotFound:       return "Driver::PluginNotFound";

            // S2P (500-599)
            case ErrorCode::S2pResourceNotFound:   return "S2P::ResourceNotFound";
            case ErrorCode::S2pConversionFailed:   return "S2P::ConversionFailed";
            case ErrorCode::S2pScriptError:        return "S2P::ScriptError";
            case ErrorCode::S2pDictionaryError:    return "S2P::DictionaryError";

            // SVS (600-699)
            case ErrorCode::SvsSingerNotFound:     return "SVS::SingerNotFound";
            case ErrorCode::SvsSingerNotLoaded:    return "SVS::SingerNotLoaded";
            case ErrorCode::SvsStageResolveFailed: return "SVS::StageResolveFailed";
            case ErrorCode::SvsCategoryNotFound:   return "SVS::CategoryNotFound";

            // Audio (700-799)
            case ErrorCode::AudioDecodeFailed:       return "Audio::DecodeFailed";
            case ErrorCode::AudioResampleFailed:     return "Audio::ResampleFailed";
            case ErrorCode::AudioUnsupportedFormat:  return "Audio::UnsupportedFormat";
            case ErrorCode::AudioInvalidBuffer:      return "Audio::InvalidBuffer";
            case ErrorCode::AudioWriteFailed:        return "Audio::WriteFailed";

            // Extract (800-899)
            case ErrorCode::ExtractNotInitialized:     return "Extract::NotInitialized";
            case ErrorCode::ExtractModelOpenFailed:    return "Extract::ModelOpenFailed";
            case ErrorCode::ExtractInferenceFailed:    return "Extract::InferenceFailed";
            case ErrorCode::ExtractOutputInvalid:      return "Extract::OutputInvalid";
            case ErrorCode::ExtractPluginNotFound:     return "Extract::PluginNotFound";
            case ErrorCode::ExtractUnsupportedVersion: return "Extract::UnsupportedVersion";
        }
        return "Unknown";
    }

    // === errorCategoryToString ===
    const char *errorCategoryToString(ErrorCategory category) noexcept {
        switch (category) {
            case ErrorCategory::None:      return "None";
            case ErrorCategory::General:   return "General";
            case ErrorCategory::Package:   return "Package";
            case ErrorCategory::Inference: return "Inference";
            case ErrorCategory::G2P:       return "G2P";
            case ErrorCategory::Driver:    return "Driver";
            case ErrorCategory::S2P:       return "S2P";
            case ErrorCategory::SVS:       return "SVS";
            case ErrorCategory::Audio:     return "Audio";
            case ErrorCategory::Extract:   return "Extract";
        }
        return "Unknown";
    }

    // === Error::formatLocation ===
    std::string Error::formatLocation(const std::source_location &loc) {
        // Only keep the filename, not the full path
        std::filesystem::path p(loc.file_name());
        return std::format("{}:{}:{}", p.filename().string(), loc.line(), loc.function_name());
    }

    // === Error::toString ===
    std::string Error::toString() const {
        if (ok()) {
            return {};
        }
        std::string result = std::format("[{}] {}", codeString(), *_msg);

        if (!_diagnostic->location.empty()) {
            result += "\n  at " + _diagnostic->location;
        }

        // Context fields
        bool hasContext = false;
        std::string ctx;
        if (!_diagnostic->singerId.empty()) {
            ctx += std::format("singerId: \"{}\"", _diagnostic->singerId);
            hasContext = true;
        }
        if (!_diagnostic->moduleId.empty()) {
            if (hasContext) ctx += ", ";
            ctx += std::format("moduleId: \"{}\"", _diagnostic->moduleId);
            hasContext = true;
        }
        if (!_diagnostic->packageId.empty()) {
            if (hasContext) ctx += ", ";
            ctx += std::format("packageId: \"{}\"", _diagnostic->packageId);
            hasContext = true;
        }
        if (!_diagnostic->language.empty()) {
            if (hasContext) ctx += ", ";
            ctx += std::format("language: \"{}\"", _diagnostic->language);
            hasContext = true;
        }
        if (!_diagnostic->providerKey.empty()) {
            if (hasContext) ctx += ", ";
            ctx += std::format("provider: \"{}\"", _diagnostic->providerKey);
            hasContext = true;
        }
        if (hasContext) {
            result += "\n  " + ctx;
        }

        // Trace
        if (!_diagnostic->trace.empty()) {
            result += "\n  trace:";
            for (const auto &t : _diagnostic->trace) {
                result += "\n    - " + t;
            }
        }

        return result;
    }

    // === Error::appendTrace ===
    void Error::appendTrace(std::string entry) {
        _diagnostic->trace.push_back(std::move(entry));
    }

    void Error::appendTrace(const std::source_location &loc, std::string note) {
        std::string entry = formatLocation(loc);
        if (!note.empty()) {
            entry = std::format("{} [{}]", note, entry);
        }
        _diagnostic->trace.push_back(std::move(entry));
    }

    // === Chainable helpers ===

    Error &Error::withTrace(const std::source_location &loc, std::string note) {
        appendTrace(loc, std::move(note));
        return *this;
    }

    Error &Error::withContext(std::string singerId, std::string moduleId,
                              std::string packageId, std::string language) {
        if (!singerId.empty()) {
            _diagnostic->singerId = std::move(singerId);
        }
        if (!moduleId.empty()) {
            _diagnostic->moduleId = std::move(moduleId);
        }
        if (!packageId.empty()) {
            _diagnostic->packageId = std::move(packageId);
        }
        if (!language.empty()) {
            _diagnostic->language = std::move(language);
        }
        return *this;
    }

    // === Factory functions ===
    Error Error::packageError(ErrorCode code, std::string msg, std::string packageId,
                              const std::source_location &loc) {
        Error err(code, std::move(msg), loc);
        if (!packageId.empty()) {
            err._diagnostic->packageId = std::move(packageId);
        }
        return err;
    }

    Error Error::inferenceError(ErrorCode code, std::string msg, std::string singerId,
                                std::string stage, const std::source_location &loc) {
        Error err(code, std::move(msg), loc);
        if (!singerId.empty()) {
            err._diagnostic->singerId = std::move(singerId);
        }
        if (!stage.empty()) {
            err._diagnostic->moduleId = std::move(stage);
        }
        return err;
    }

    Error Error::g2pError(ErrorCode code, std::string msg, std::string language,
                          std::string packageId, const std::source_location &loc) {
        Error err(code, std::move(msg), loc);
        if (!language.empty()) {
            err._diagnostic->language = std::move(language);
        }
        if (!packageId.empty()) {
            err._diagnostic->packageId = std::move(packageId);
        }
        return err;
    }

    // === defaultDiagnostic (ErrorCode overload, with source_location) ===
    std::shared_ptr<Diagnostic> Error::defaultDiagnostic(
        ErrorCode code, const std::string &message, const std::source_location &loc) {
        auto diagnostic = std::make_shared<Diagnostic>();
        diagnostic->code = code;
        diagnostic->message = message;
        diagnostic->location = formatLocation(loc);
        return diagnostic;
    }

    // === Legacy defaultMessage ===
    std::shared_ptr<std::string> Error::defaultMessage(int type) {
        switch (type) {
            case NoError: {
                static auto message = std::make_shared<std::string>();
                return message;
            }
            case InvalidFormat: {
                static auto message = std::make_shared<std::string>("invalid format");
                return message;
            }
            case FileNotFound: {
                static auto message = std::make_shared<std::string>("file not found");
                return message;
            }
            case FileNotOpen: {
                static auto message = std::make_shared<std::string>("file not open");
                return message;
            }
            case FileDuplicated: {
                static auto message = std::make_shared<std::string>("file duplicated");
                return message;
            }
            case RecursiveDependency: {
                static auto message = std::make_shared<std::string>("recursive dependency");
                return message;
            }
            case FeatureNotSupported: {
                static auto message = std::make_shared<std::string>("feature not supported");
                return message;
            }
            case InvalidArgument: {
                static auto message = std::make_shared<std::string>("invalid argument");
                return message;
            }
            case NotImplemented: {
                static auto message = std::make_shared<std::string>("not implemented");
                return message;
            }
            case SessionError: {
                static auto message = std::make_shared<std::string>("session error");
                return message;
            }
            default:
                break;
        }
        static auto message = std::make_shared<std::string>("unknown error");
        return message;
    }

    // === Legacy defaultDiagnostic (int type overload) ===
    std::shared_ptr<Diagnostic> Error::defaultDiagnostic(int type, const std::string &message) {
        auto diagnostic = std::make_shared<Diagnostic>();
        diagnostic->message = message;
        switch (type) {
            case NoError:
                diagnostic->code = ErrorCode::None;
                diagnostic->severity = Severity::Info;
                break;
            case InvalidFormat:
                diagnostic->code = ErrorCode::InvalidFormat;
                break;
            case FileNotFound:
                diagnostic->code = ErrorCode::FileNotFound;
                break;
            case FileNotOpen:
                diagnostic->code = ErrorCode::FileNotOpen;
                break;
            case FileDuplicated:
                diagnostic->code = ErrorCode::FileDuplicated;
                break;
            case RecursiveDependency:
                diagnostic->code = ErrorCode::RecursiveDependency;
                break;
            case FeatureNotSupported:
                diagnostic->code = ErrorCode::FeatureNotSupported;
                break;
            case InvalidArgument:
                diagnostic->code = ErrorCode::InvalidArgument;
                break;
            case NotImplemented:
                diagnostic->code = ErrorCode::NotImplemented;
                break;
            case SessionError:
                diagnostic->code = ErrorCode::SessionError;
                break;
            default:
                diagnostic->code = ErrorCode::InvalidArgument;
                break;
        }
        return diagnostic;
    }

    // === typeFromCode (maps ErrorCode to legacy Type) ===
    int Error::typeFromCode(ErrorCode code) {
        auto cat = errorCodeCategory(code);
        switch (cat) {
            case ErrorCategory::None:
                return NoError;
            case ErrorCategory::General:
                switch (code) {
                    case ErrorCode::None:               return NoError;
                    case ErrorCode::InvalidFormat:      return InvalidFormat;
                    case ErrorCode::FileNotFound:       return FileNotFound;
                    case ErrorCode::FileNotOpen:        return FileNotOpen;
                    case ErrorCode::FileDuplicated:     return FileDuplicated;
                    case ErrorCode::RecursiveDependency:return RecursiveDependency;
                    case ErrorCode::FeatureNotSupported:return FeatureNotSupported;
                    case ErrorCode::InvalidArgument:    return InvalidArgument;
                    case ErrorCode::NotImplemented:     return NotImplemented;
                    case ErrorCode::SessionError:       return SessionError;
                    default:                            return InvalidArgument;
                }
            case ErrorCategory::Package:
                switch (code) {
                    case ErrorCode::PackageManifestInvalid:
                    case ErrorCode::PackageManifestMissingField:
                    case ErrorCode::PackageManifestNotFound:
                        return InvalidFormat;
                    case ErrorCode::PackageDependencyMissing:
                    case ErrorCode::PackageDependencyCycle:
                        return RecursiveDependency;
                    case ErrorCode::PackageRootInvalid:
                    case ErrorCode::PackageSourceAfterInitialize:
                    case ErrorCode::PackageScanAfterInitialize:
                    case ErrorCode::PackageVersionConflict:
                    case ErrorCode::PackageSingerConfigInvalid:
                    case ErrorCode::PackageSingerConfigMissing:
                    case ErrorCode::PackageSingerIdEmpty:
                    case ErrorCode::PackageDuplicate:
                        return InvalidArgument;
                    default:
                        return InvalidArgument;
                }
            case ErrorCategory::Inference:
                switch (code) {
                    case ErrorCode::InferenceInputInvalid:
                    case ErrorCode::InferenceStageSpecNull:
                        return InvalidArgument;
                    case ErrorCode::InferenceNotInitialized:
                    case ErrorCode::InferenceModelNotFound:
                    case ErrorCode::InferenceModelLoadFailed:
                    case ErrorCode::InferenceModelInitFailed:
                    case ErrorCode::InferenceStartFailed:
                    case ErrorCode::InferenceRunFailed:
                    case ErrorCode::InferenceStageMissing:
                    case ErrorCode::InferenceSpeakerNotFound:
                    case ErrorCode::InferenceTensorCreateFailed:
                    case ErrorCode::InferenceOutputEmpty:
                    case ErrorCode::InferenceDataTypeMismatch:
                    case ErrorCode::InferenceSampleRateMismatch:
                    case ErrorCode::InferenceAlreadyRunning:
                        return SessionError;
                    default:
                        return SessionError;
                }
            case ErrorCategory::G2P:
                switch (code) {
                    case ErrorCode::G2pSuccess:   return NoError;   // fix G2pSuccess slicing bug (ER-02)
                    case ErrorCode::G2pConfigError:
                    case ErrorCode::G2pValidationError:
                        return InvalidFormat;
                    case ErrorCode::G2pFileSystemError:
                        return FileNotFound;
                    case ErrorCode::G2pDependencyError:
                        return RecursiveDependency;
                    case ErrorCode::G2pNotImplementedError:
                        return NotImplemented;
                    case ErrorCode::G2pRuntimeError:
                    case ErrorCode::G2pInitializationError:
                    case ErrorCode::G2pNullPointerError:
                    case ErrorCode::G2pIndexError:
                    case ErrorCode::G2pTimeoutError:
                    case ErrorCode::G2pAlreadyInitialized:
                    case ErrorCode::G2pRouteNotFound:
                    case ErrorCode::G2pPackageNotFound:
                    case ErrorCode::G2pPluginNotFound:
                    case ErrorCode::G2pDriverNotFound:
                    case ErrorCode::G2pDriverInitFailed:
                    case ErrorCode::G2pConversionFailed:
                    case ErrorCode::G2pSessionError:
                    case ErrorCode::G2pContextNotFound:
                    case ErrorCode::G2pTaskNotFound:
                        return SessionError;
                    default:
                        return SessionError;
                }
            case ErrorCategory::Driver:
                return SessionError;
            case ErrorCategory::S2P:
                return InvalidFormat;
            case ErrorCategory::SVS:
                return InvalidArgument;
            case ErrorCategory::Audio:
                return SessionError;
            case ErrorCategory::Extract:
                return SessionError;
        }
        return InvalidArgument;
    }

}
