#ifndef SRT_G2P_SUPPORT_ERROR_H
#define SRT_G2P_SUPPORT_ERROR_H

#include <memory>
#include <source_location>
#include <string>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/G2P/srt_g2p_global.h>

namespace srt::g2p {

    using srt::core::ErrorCode;

    /// Error - G2P domain error type.
    ///
    /// Inherits from srt::core::Error to allow seamless use with
    /// srt::core::Expected<T> (slicing is safe: same layout, no virtual
    /// members, no additional data members beyond _suggestion).
    ///
    /// The legacy Type enum is deprecated; new code should use
    /// srt::core::ErrorCode (G2p* codes, 300-399). Old constructors map
    /// Type → ErrorCode internally.
    ///
    /// Migrated from LangCore::Error (12 types preserved per D11/D12).
    class SRT_G2P_EXPORT Error : public srt::core::Error {
    public:
        [[deprecated("use srt::core::ErrorCode::G2p* instead")]] enum Type {
            Success = 0,
            ConfigError,             // 配置错误（JSON 格式错误、参数错误等）
            FileSystemError,         // 文件系统错误（文件未找到、无法打开、重复加载等）
            DependencyError,         // 依赖错误（循环依赖、依赖未找到、解释器未找到等）
            RuntimeError,            // 运行时错误（会话错误、任务错误、运行时异常等）
            NotImplementedError,     // 未实现错误（功能不支持、方法未实现等）
            InitializationError,     // 初始化错误（未初始化、初始化失败等）
            ValidationError,         // 验证错误（参数验证失败、数据验证失败等）
            NullPointerError,        // 空指针错误（nullptr 访问、无效指针等）
            IndexError,              // 索引错误（数组越界、无效索引等）
            TimeoutError,            // 超时错误（操作超时、响应超时等）
            AlreadyInitialized,      // 已初始化错误（Manager::initialize() 重复调用等，D11 硬幂等）
        };

        // === New constructors (ErrorCode + auto source_location) ===

        Error(ErrorCode code, std::string msg,
              const std::source_location &loc = std::source_location::current());

        Error(ErrorCode code, const char *msg,
              const std::source_location &loc = std::source_location::current());

        /// Constructor with suggestion (migrated from LangCore::Error).
        Error(ErrorCode code, std::string msg, std::string suggestion,
              const std::source_location &loc = std::source_location::current());

        Error(ErrorCode code, const char *msg, const char *suggestion,
              const std::source_location &loc = std::source_location::current());

        // === Default constructor (uses ErrorCode, not deprecated) ===
        Error() : Error(ErrorCode::G2pSuccess, std::string{}) {}

        // === Legacy constructors (deprecated, map Type → ErrorCode) ===

        [[deprecated]] Error(Type type);

        [[deprecated]] Error(Type type, std::string msg);

        [[deprecated]] Error(Type type, const char *msg);

        [[deprecated]] Error(Type type, std::string msg, std::string suggestion);

        [[deprecated]] Error(Type type, const char *msg, const char *suggestion);

        /// Recover the G2P-specific error type (deprecated).
        [[deprecated]] Type g2pType() const { return static_cast<Type>(srt::core::Error::type()); }

        /// G2P-specific ok check (G2pSuccess is semantically NoError).
        bool ok() const { return code() == ErrorCode::G2pSuccess; }

        /// Suggestion accessor (returns empty string if no suggestion is set).
        const std::string &suggestion() const {
            static const std::string emptySuggestion;
            return _suggestion ? *_suggestion : emptySuggestion;
        }

        bool hasSuggestion() const { return _suggestion != nullptr; }

        static Error success() { return Error(ErrorCode::G2pSuccess, std::string{}); }

        [[deprecated]] static std::shared_ptr<std::string> defaultMessage(Type type);

    protected:
        std::shared_ptr<std::string> _suggestion;
    };

} // namespace srt::g2p

#endif // SRT_G2P_SUPPORT_ERROR_H
