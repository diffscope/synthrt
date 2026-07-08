#ifndef SRT_S2P_INTERNAL_LUAEXECUTIONENVIRONMENT_H
#define SRT_S2P_INTERNAL_LUAEXECUTIONENVIRONMENT_H

#include <memory>
#include <string>
#include <vector>

struct lua_State;

namespace srt::s2p {

    class LuaScript;

    namespace Internal {

        /// Dispatches a Lua `print(...)` call to the globally registered
        /// LuaScript::PrintHandler. Implemented in LuaScript.cpp.
        void dispatchLuaPrint(const std::string &chunkName, const std::vector<std::string> &arguments);

        /// LuaExecutionEnvironment owns a `lua_State` and enforces the
        /// sandbox: standard libs are loaded but `io`/`os`/`debug`/`package`/
        /// `require`/`module`/`dofile`/`loadfile` are nilled, a `utf8` library
        /// is registered, and a count-based interrupt hook allows cooperative
        /// cancellation via `interrupt()`.
        ///
        /// This is an internal implementation detail and is not exported in
        /// the public srt::s2p API surface.
        class LuaExecutionEnvironment {
        public:
            LuaExecutionEnvironment();
            ~LuaExecutionEnvironment();

            LuaExecutionEnvironment(const LuaExecutionEnvironment &) = delete;
            LuaExecutionEnvironment &operator=(const LuaExecutionEnvironment &) = delete;
            LuaExecutionEnvironment(LuaExecutionEnvironment &&) noexcept;
            LuaExecutionEnvironment &operator=(LuaExecutionEnvironment &&) noexcept;

            lua_State *state() const;

            void interrupt() noexcept;
            int pcall(int nargs, int nresults, int msgh = 0);
            void loadAndRun(const LuaScript &script);
            int getGlobalFunctionRef(const std::string &name);
            void pushRef(int ref) const;
            void releaseRef(int ref);
            std::string errorString(int index = -1) const;

        private:
            class Private;
            std::unique_ptr<Private> d;
        };

    }

}

#endif // SRT_S2P_INTERNAL_LUAEXECUTIONENVIRONMENT_H
