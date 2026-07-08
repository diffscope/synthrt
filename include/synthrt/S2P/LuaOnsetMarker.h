#ifndef SRT_S2P_LUAONSETMARKER_H
#define SRT_S2P_LUAONSETMARKER_H

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>

#include <synthrt/S2P/srt_s2p_global.h>

namespace srt::s2p {

    class LuaScript;

    /// LuaOnsetMarker marks onset positions by calling the Lua global function
    /// `markonset(phonemes)` and reading the returned table of booleans.
    ///
    /// `mark` is a `const` instance method: each LuaOnsetMarker owns its own
    /// LuaExecutionEnvironment (and thus lua_State) for the lifetime of the
    /// instance.
    class SRT_S2P_EXPORT LuaOnsetMarker {
    public:
        /// Creates a LuaOnsetMarker bound to the given compiled @p luaScript.
        /// Returns an `Expected` error (InvalidFormat) on load/run failure
        /// instead of throwing.
        static srt::core::Expected<std::unique_ptr<LuaOnsetMarker>> create(const LuaScript &luaScript);

        ~LuaOnsetMarker();

        LuaOnsetMarker(const LuaOnsetMarker &) = delete;
        LuaOnsetMarker &operator=(const LuaOnsetMarker &) = delete;
        LuaOnsetMarker(LuaOnsetMarker &&) noexcept;
        LuaOnsetMarker &operator=(LuaOnsetMarker &&) noexcept;

        void interrupt() const noexcept;

        /// Calls the Lua `markonset` function and returns the boolean list, or
        /// an `Expected` error (InvalidFormat) on Lua/runtime failure.
        srt::core::Expected<std::vector<bool>> mark(const std::vector<std::string> &phonemeSequence) const;

    private:
        LuaOnsetMarker();

        class Private;
        std::unique_ptr<Private> d;
    };

}

#endif // SRT_S2P_LUAONSETMARKER_H
