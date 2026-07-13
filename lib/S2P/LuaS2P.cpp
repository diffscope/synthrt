#include <synthrt/S2P/LuaS2P.h>

#include "internal/LuaExecutionEnvironment.h"
#include <synthrt/S2P/LuaScript.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef SRT_S2P_HAS_LUA
#include <lua.hpp>
#endif

namespace srt::s2p {

#ifdef SRT_S2P_HAS_LUA
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

    class LuaS2P::Private {
    public:
        Private() = default;

        Private(Private &&other) noexcept
            : environment(std::move(other.environment)), s2pRef(other.s2pRef) {
            other.s2pRef = LUA_NOREF;
        }

        Private &operator=(Private &&other) noexcept {
            if (this != &other) {
                if (s2pRef != LUA_NOREF) {
                    environment.releaseRef(s2pRef);
                }
                environment = std::move(other.environment);
                s2pRef = other.s2pRef;
                other.s2pRef = LUA_NOREF;
            }
            return *this;
        }

        ~Private() {
            if (s2pRef != LUA_NOREF) {
                environment.releaseRef(s2pRef);
            }
        }

        Internal::LuaExecutionEnvironment environment;
        int s2pRef{LUA_NOREF};
    };

    LuaS2P::LuaS2P() : d(std::make_unique<Private>()) {
    }

    LuaS2P::~LuaS2P() = default;

    LuaS2P::LuaS2P(LuaS2P &&) noexcept = default;

    LuaS2P &LuaS2P::operator=(LuaS2P &&) noexcept = default;

    srt::core::Expected<std::unique_ptr<LuaS2P>> LuaS2P::create(const LuaScript &luaScript) {
        std::unique_ptr<LuaS2P> obj;

        try {
            // Construct inside try block: LuaExecutionEnvironment constructor may
            // throw std::runtime_error if luaL_newstate() fails (OOM).
            obj = std::unique_ptr<LuaS2P>(new LuaS2P());
            obj->d->environment.loadAndRun(luaScript);
            obj->d->s2pRef = obj->d->environment.getGlobalFunctionRef("s2p");
        } catch (const std::exception &e) {
            return srt::core::Error(srt::core::ErrorCode::S2pScriptError, e.what());
        }

        return obj;
    }

    void LuaS2P::interrupt() const noexcept {
        d->environment.interrupt();
    }

    srt::core::Expected<std::vector<std::string>>
    LuaS2P::convert(std::string_view pronunciation) const {
        auto *state = d->environment.state();
        StackGuard guard(state);

        d->environment.pushRef(d->s2pRef);
        lua_pushlstring(state, pronunciation.data(), pronunciation.size());

        if (d->environment.pcall(1, 1, 0) != 0) {
            return srt::core::Error(srt::core::ErrorCode::S2pScriptError,
                "LuaS2P error: s2p failed: " + d->environment.errorString());
        }

        if (!lua_istable(state, -1)) {
            return srt::core::Error(srt::core::ErrorCode::S2pScriptError,
                "LuaS2P error: s2p must return a table");
        }

        const auto length = lua_objlen(state, -1);
        std::vector<std::string> result;
        result.reserve(static_cast<std::size_t>(length));

        for (std::size_t i = 1; i <= length; ++i) {
            lua_rawgeti(state, -1, static_cast<int>(i));
            if (lua_type(state, -1) != LUA_TSTRING) {
                return srt::core::Error(srt::core::ErrorCode::S2pScriptError,
                    "LuaS2P error: s2p return value contains a non-string element at index " +
                        std::to_string(i));
            }

            std::size_t size = 0;
            const auto *data = lua_tolstring(state, -1, &size);
            result.emplace_back(data, size);
            lua_pop(state, 1);
        }

        return result;
    }

#else

    class LuaS2P::Private {};

    LuaS2P::LuaS2P() : d(std::make_unique<Private>()) {
    }

    LuaS2P::~LuaS2P() = default;
    LuaS2P::LuaS2P(LuaS2P &&) noexcept = default;
    LuaS2P &LuaS2P::operator=(LuaS2P &&) noexcept = default;

    srt::core::Expected<std::unique_ptr<LuaS2P>> LuaS2P::create(const LuaScript &) {
        return srt::core::Error(srt::core::ErrorCode::FeatureNotSupported,
            "LuaS2P is unavailable because synthrt was built without LuaJIT support");
    }

    void LuaS2P::interrupt() const noexcept {
    }

    srt::core::Expected<std::vector<std::string>>
    LuaS2P::convert(std::string_view) const {
        return srt::core::Error(srt::core::ErrorCode::FeatureNotSupported,
            "LuaS2P is unavailable because synthrt was built without LuaJIT support");
    }

#endif

}
