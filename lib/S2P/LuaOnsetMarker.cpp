#include <synthrt/S2P/LuaOnsetMarker.h>

#include "internal/LuaExecutionEnvironment.h"
#include <synthrt/S2P/LuaScript.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <lua.hpp>

namespace srt::s2p {

    namespace {

        class StackGuard {
        public:
            explicit StackGuard(lua_State *state) : m_state(state), m_top(lua_gettop(state)) {
            }

            ~StackGuard() {
                lua_settop(m_state, m_top);
            }

        private:
            lua_State *m_state;
            int m_top;
        };

    }

    class LuaOnsetMarker::Private {
    public:
        Private() = default;

        Private(Private &&other) noexcept
            : environment(std::move(other.environment)), markOnsetRef(other.markOnsetRef) {
            other.markOnsetRef = LUA_NOREF;
        }

        Private &operator=(Private &&other) noexcept {
            if (this != &other) {
                if (markOnsetRef != LUA_NOREF) {
                    environment.releaseRef(markOnsetRef);
                }
                environment = std::move(other.environment);
                markOnsetRef = other.markOnsetRef;
                other.markOnsetRef = LUA_NOREF;
            }
            return *this;
        }

        ~Private() {
            if (markOnsetRef != LUA_NOREF) {
                environment.releaseRef(markOnsetRef);
            }
        }

        Internal::LuaExecutionEnvironment environment;
        int markOnsetRef{LUA_NOREF};
    };

    LuaOnsetMarker::LuaOnsetMarker() : d(std::make_unique<Private>()) {
    }

    LuaOnsetMarker::~LuaOnsetMarker() = default;

    LuaOnsetMarker::LuaOnsetMarker(LuaOnsetMarker &&) noexcept = default;

    LuaOnsetMarker &LuaOnsetMarker::operator=(LuaOnsetMarker &&) noexcept = default;

    srt::core::Expected<std::unique_ptr<LuaOnsetMarker>>
    LuaOnsetMarker::create(const LuaScript &luaScript) {
        std::unique_ptr<LuaOnsetMarker> obj;

        try {
            // Construct inside try block: LuaExecutionEnvironment constructor may
            // throw std::runtime_error if luaL_newstate() fails (OOM).
            obj = std::unique_ptr<LuaOnsetMarker>(new LuaOnsetMarker());
            obj->d->environment.loadAndRun(luaScript);
            obj->d->markOnsetRef = obj->d->environment.getGlobalFunctionRef("markonset");
        } catch (const std::exception &e) {
            return srt::core::Error(srt::core::ErrorCode::S2pScriptError, e.what());
        }

        return obj;
    }

    void LuaOnsetMarker::interrupt() const noexcept {
        d->environment.interrupt();
    }

    srt::core::Expected<std::vector<bool>>
    LuaOnsetMarker::mark(const std::vector<std::string> &phonemeSequence) const {
        auto *state = d->environment.state();
        StackGuard guard(state);

        d->environment.pushRef(d->markOnsetRef);

        lua_createtable(state, static_cast<int>(phonemeSequence.size()), 0);
        for (std::size_t i = 0; i < phonemeSequence.size(); ++i) {
            const auto &phoneme = phonemeSequence[i];
            lua_pushlstring(state, phoneme.data(), phoneme.size());
            lua_rawseti(state, -2, static_cast<int>(i + 1));
        }

        if (d->environment.pcall(1, 1, 0) != 0) {
            return srt::core::Error(srt::core::ErrorCode::S2pScriptError,
                "LuaOnsetMarker error: markonset failed: " + d->environment.errorString());
        }

        if (!lua_istable(state, -1)) {
            return srt::core::Error(srt::core::ErrorCode::S2pScriptError,
                "LuaOnsetMarker error: markonset must return a table");
        }

        const auto length = lua_objlen(state, -1);
        if (length != phonemeSequence.size()) {
            return srt::core::Error(srt::core::ErrorCode::S2pScriptError,
                "LuaOnsetMarker error: markonset must return a table with the same length as the input");
        }

        std::vector<bool> result;
        result.reserve(phonemeSequence.size());

        for (std::size_t i = 1; i <= length; ++i) {
            lua_rawgeti(state, -1, static_cast<int>(i));
            if (lua_type(state, -1) != LUA_TBOOLEAN) {
                return srt::core::Error(srt::core::ErrorCode::S2pScriptError,
                    "LuaOnsetMarker error: markonset return value contains a non-boolean element at index " +
                        std::to_string(i));
            }

            result.push_back(lua_toboolean(state, -1) != 0);
            lua_pop(state, 1);
        }

        return result;
    }

}
