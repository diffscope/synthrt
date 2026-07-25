#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

#include <synthrt/S2P/srt_s2p_global.h>

namespace srt::s2p {

    /// LuaScript compiles a Lua source string into bytecode once, so multiple
    /// LuaS2P / LuaOnsetMarker instances can share the same compiled chunk
    /// without re-parsing.
    ///
    /// The sandbox is enforced by LuaExecutionEnvironment (Internal): `io`,
    /// `os`, `debug`, `package`, `require`, `module`, `dofile`, `loadfile` are
    /// set to nil; an interrupt hook allows cooperative cancellation.
    class SRT_S2P_EXPORT LuaScript {
    public:
        using PrintHandler = std::function<void(const std::string &, const std::vector<std::string> &)>;

        /// Compiles @p script with the given @p chunkName (used in error
        /// messages and the sandbox `print` handler). Returns an `Expected`
        /// error (InvalidFormat) on compile/dump failure instead of throwing.
        static srt::core::Expected<std::unique_ptr<LuaScript>> create(const std::string &script,
                                                                      const std::string &chunkName);

        ~LuaScript();

        LuaScript(const LuaScript &) = delete;
        LuaScript &operator=(const LuaScript &) = delete;
        LuaScript(LuaScript &&) noexcept;
        LuaScript &operator=(LuaScript &&) noexcept;

        static void setLuaPrintHandler(PrintHandler handler);
        static PrintHandler luaPrintHandler();
        static void setInterruptHookCountInterval(int interval);
        static int interruptHookCountInterval();

        const std::vector<unsigned char> &byteCode() const;
        const std::string &chunkName() const;

    private:
        LuaScript();

        class Private;
        std::unique_ptr<Private> d;
    };

}
