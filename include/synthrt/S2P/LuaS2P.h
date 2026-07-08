#ifndef SRT_S2P_LUAS2P_H
#define SRT_S2P_LUAS2P_H

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

#include <synthrt/S2P/srt_s2p_global.h>

namespace srt::s2p {

    class LuaScript;

    /// LuaS2P converts a pronunciation by calling the Lua global function
    /// `s2p(pronunciation)` and reading the returned table of phoneme strings.
    ///
    /// `convert` is a `const` instance method: each LuaS2P owns its own
    /// LuaExecutionEnvironment (and thus lua_State) for the lifetime of the
    /// instance.
    class SRT_S2P_EXPORT LuaS2P {
    public:
        /// Creates a LuaS2P bound to the given compiled @p luaScript.
        /// Returns an `Expected` error (InvalidFormat) on load/run failure
        /// instead of throwing.
        static srt::core::Expected<std::unique_ptr<LuaS2P>> create(const LuaScript &luaScript);

        ~LuaS2P();

        LuaS2P(const LuaS2P &) = delete;
        LuaS2P &operator=(const LuaS2P &) = delete;
        LuaS2P(LuaS2P &&) noexcept;
        LuaS2P &operator=(LuaS2P &&) noexcept;

        void interrupt() const noexcept;

        /// Calls the Lua `s2p` function and returns the phoneme list, or an
        /// `Expected` error (InvalidFormat) on Lua/runtime failure.
        srt::core::Expected<std::vector<std::string>> convert(std::string_view pronunciation) const;

    private:
        LuaS2P();

        class Private;
        std::unique_ptr<Private> d;
    };

}

#endif // SRT_S2P_LUAS2P_H
