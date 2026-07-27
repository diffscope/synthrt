#include "Support/Error.h"

namespace srt::g2p {

    // === ErrorCode constructors (delegate to parent) ===

    Error::Error(ErrorCode code, std::string msg, const std::source_location &loc)
        : srt::core::Error(code, std::move(msg), loc) {}

    Error::Error(ErrorCode code, const char *msg, const std::source_location &loc)
        : srt::core::Error(code, msg, loc) {}

    Error::Error(ErrorCode code, std::string msg, std::string suggestion,
                 const std::source_location &loc)
        : srt::core::Error(code, std::move(msg), loc),
          _suggestion(std::make_shared<std::string>(std::move(suggestion))) {}

    Error::Error(ErrorCode code, const char *msg, const char *suggestion,
                 const std::source_location &loc)
        : srt::core::Error(code, msg, loc),
          _suggestion(std::make_shared<std::string>(suggestion)) {}

} // namespace srt::g2p
