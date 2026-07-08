#include "internal/LuaExecutionEnvironment.h"

#include <synthrt/S2P/LuaScript.h>

#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <lua.hpp>

namespace srt::s2p::Internal {

    namespace {

        char interruptFlagRegistryKey;

        void setGlobalNil(lua_State *state, const char *name) {
            lua_pushnil(state);
            lua_setglobal(state, name);
        }

        int sandboxPrint(lua_State *state) {
            lua_getfield(state, lua_upvalueindex(1), "chunkName");
            const auto *chunkNameData = lua_tostring(state, -1);
            const std::string chunkName = chunkNameData ? chunkNameData : std::string{};
            lua_pop(state, 1);

            std::vector<std::string> arguments;
            const auto count = lua_gettop(state);
            arguments.reserve(static_cast<std::size_t>(count));

            lua_getglobal(state, "tostring");
            const auto tostringIndex = lua_gettop(state);

            for (auto i = 1; i <= count; ++i) {
                lua_pushvalue(state, tostringIndex);
                lua_pushvalue(state, i);
                lua_call(state, 1, 1);

                std::size_t length = 0;
                const auto *text = lua_tolstring(state, -1, &length);
                arguments.emplace_back(text ? std::string(text, length) : std::string{});
                lua_pop(state, 1);
            }

            dispatchLuaPrint(chunkName, arguments);
            return 0;
        }

        int interruptLua(lua_State *state) {
            return luaL_error(state, "Lua execution interrupted");
        }

        void interruptHook(lua_State *state, lua_Debug *) {
            lua_pushlightuserdata(state, &interruptFlagRegistryKey);
            lua_gettable(state, LUA_REGISTRYINDEX);
            auto *interrupted = static_cast<std::atomic_bool *>(lua_touserdata(state, -1));
            lua_pop(state, 1);

            if (interrupted && interrupted->load(std::memory_order_acquire)) {
                interruptLua(state);
            }
        }

        bool readCodePoint(const char *data, std::size_t size, std::size_t offset, unsigned int &codePoint, std::size_t &nextOffset) {
            if (offset >= size) {
                return false;
            }

            const auto first = static_cast<unsigned char>(data[offset]);
            if (first < 0x80) {
                codePoint = first;
                nextOffset = offset + 1;
                return true;
            }

            std::size_t length = 0;
            unsigned int value = 0;
            if ((first & 0xE0) == 0xC0) {
                length = 2;
                value = first & 0x1F;
                if (value == 0) {
                    return false;
                }
            } else if ((first & 0xF0) == 0xE0) {
                length = 3;
                value = first & 0x0F;
            } else if ((first & 0xF8) == 0xF0) {
                length = 4;
                value = first & 0x07;
            } else {
                return false;
            }

            if (offset + length > size) {
                return false;
            }

            for (std::size_t i = 1; i < length; ++i) {
                const auto ch = static_cast<unsigned char>(data[offset + i]);
                if ((ch & 0xC0) != 0x80) {
                    return false;
                }
                value = (value << 6) | (ch & 0x3F);
            }

            if ((length == 2 && value < 0x80) ||
                (length == 3 && value < 0x800) ||
                (length == 4 && value < 0x10000) ||
                value > 0x10FFFF ||
                (value >= 0xD800 && value <= 0xDFFF)) {
                return false;
            }

            codePoint = value;
            nextOffset = offset + length;
            return true;
        }

        void pushCodePoint(std::string &out, unsigned int codePoint) {
            if (codePoint <= 0x7F) {
                out.push_back(static_cast<char>(codePoint));
            } else if (codePoint <= 0x7FF) {
                out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            } else if (codePoint <= 0xFFFF) {
                out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
        }

        int normalizePosition(lua_State *state, int index, std::size_t size, int defaultValue) {
            const auto position = static_cast<long long>(luaL_optinteger(state, index, defaultValue));
            if (position > 0) {
                return static_cast<int>(position);
            }
            if (position < 0) {
                return static_cast<int>(static_cast<long long>(size) + position + 1);
            }
            return 0;
        }

        int utf8Char(lua_State *state) {
            std::string result;
            const auto count = lua_gettop(state);
            for (auto i = 1; i <= count; ++i) {
                const auto value = static_cast<unsigned int>(luaL_checkinteger(state, i));
                if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
                    return luaL_error(state, "value out of range");
                }
                pushCodePoint(result, value);
            }
            lua_pushlstring(state, result.data(), result.size());
            return 1;
        }

        int utf8CodePoint(lua_State *state) {
            std::size_t size = 0;
            const auto *data = luaL_checklstring(state, 1, &size);
            auto start = normalizePosition(state, 2, size, 1);
            auto end = normalizePosition(state, 3, size, start);

            if (start < 1) {
                start = 1;
            }
            if (end > static_cast<int>(size)) {
                end = static_cast<int>(size);
            }
            if (start > end) {
                return 0;
            }

            auto offset = static_cast<std::size_t>(start - 1);
            const auto limit = static_cast<std::size_t>(end);
            auto count = 0;
            while (offset < limit) {
                unsigned int codePoint = 0;
                std::size_t nextOffset = 0;
                if (!readCodePoint(data, size, offset, codePoint, nextOffset) || nextOffset > limit) {
                    return luaL_error(state, "invalid UTF-8 code");
                }
                lua_pushinteger(state, static_cast<lua_Integer>(codePoint));
                ++count;
                offset = nextOffset;
            }
            return count;
        }

        int utf8Len(lua_State *state) {
            std::size_t size = 0;
            const auto *data = luaL_checklstring(state, 1, &size);
            auto start = normalizePosition(state, 2, size, 1);
            auto end = normalizePosition(state, 3, size, static_cast<int>(size));

            if (start < 1) {
                start = 1;
            }
            if (end > static_cast<int>(size)) {
                end = static_cast<int>(size);
            }
            if (start > end) {
                lua_pushinteger(state, 0);
                return 1;
            }

            auto offset = static_cast<std::size_t>(start - 1);
            const auto limit = static_cast<std::size_t>(end);
            auto count = 0;
            while (offset < limit) {
                unsigned int codePoint = 0;
                std::size_t nextOffset = 0;
                if (!readCodePoint(data, size, offset, codePoint, nextOffset) || nextOffset > limit) {
                    lua_pushnil(state);
                    lua_pushinteger(state, static_cast<lua_Integer>(offset + 1));
                    return 2;
                }
                ++count;
                offset = nextOffset;
            }

            lua_pushinteger(state, count);
            return 1;
        }

        int utf8Offset(lua_State *state) {
            std::size_t size = 0;
            const auto *data = luaL_checklstring(state, 1, &size);
            const auto n = static_cast<long long>(luaL_checkinteger(state, 2));
            auto position = normalizePosition(state, 3, size, n >= 0 ? 1 : static_cast<int>(size) + 1);

            if (position < 1) {
                position = 1;
            }
            if (position > static_cast<int>(size) + 1) {
                position = static_cast<int>(size) + 1;
            }

            if (n == 0) {
                auto offset = static_cast<std::size_t>(position - 1);
                while (offset > 0 && (static_cast<unsigned char>(data[offset]) & 0xC0) == 0x80) {
                    --offset;
                }
                lua_pushinteger(state, static_cast<lua_Integer>(offset + 1));
                return 1;
            }

            std::vector<std::size_t> starts;
            for (std::size_t offset = 0; offset < size;) {
                unsigned int codePoint = 0;
                std::size_t nextOffset = 0;
                if (!readCodePoint(data, size, offset, codePoint, nextOffset)) {
                    lua_pushnil(state);
                    return 1;
                }
                starts.push_back(offset + 1);
                offset = nextOffset;
            }

            if (n > 0) {
                std::size_t current = 0;
                while (current < starts.size() && starts[current] < static_cast<std::size_t>(position)) {
                    ++current;
                }
                const auto target = current + static_cast<std::size_t>(n) - 1;
                if (target >= starts.size()) {
                    lua_pushnil(state);
                    return 1;
                }
                lua_pushinteger(state, static_cast<lua_Integer>(starts[target]));
                return 1;
            }

            std::size_t current = starts.size();
            while (current > 0 && starts[current - 1] >= static_cast<std::size_t>(position)) {
                --current;
            }
            const auto distance = static_cast<std::size_t>(-n);
            if (distance > current) {
                lua_pushnil(state);
                return 1;
            }
            lua_pushinteger(state, static_cast<lua_Integer>(starts[current - distance]));
            return 1;
        }

        int utf8CodesIterator(lua_State *state) {
            std::size_t size = 0;
            const auto *data = luaL_checklstring(state, lua_upvalueindex(1), &size);
            auto offset = static_cast<std::size_t>(lua_tointeger(state, lua_upvalueindex(2)));

            if (offset >= size) {
                return 0;
            }

            unsigned int codePoint = 0;
            std::size_t nextOffset = 0;
            if (!readCodePoint(data, size, offset, codePoint, nextOffset)) {
                return luaL_error(state, "invalid UTF-8 code");
            }

            lua_pushinteger(state, static_cast<lua_Integer>(nextOffset));
            lua_replace(state, lua_upvalueindex(2));

            lua_pushinteger(state, static_cast<lua_Integer>(offset + 1));
            lua_pushinteger(state, static_cast<lua_Integer>(codePoint));
            return 2;
        }

        int utf8Codes(lua_State *state) {
            luaL_checkstring(state, 1);
            lua_pushvalue(state, 1);
            lua_pushinteger(state, 0);
            lua_pushcclosure(state, utf8CodesIterator, 2);
            lua_pushvalue(state, 1);
            lua_pushinteger(state, 0);
            return 3;
        }

        void registerUtf8(lua_State *state) {
            lua_newtable(state);

            lua_pushcfunction(state, utf8Char);
            lua_setfield(state, -2, "char");

            lua_pushcfunction(state, utf8CodePoint);
            lua_setfield(state, -2, "codepoint");

            lua_pushcfunction(state, utf8Codes);
            lua_setfield(state, -2, "codes");

            lua_pushcfunction(state, utf8Len);
            lua_setfield(state, -2, "len");

            lua_pushcfunction(state, utf8Offset);
            lua_setfield(state, -2, "offset");

            lua_pushliteral(state, "[\0-\x7F\xC2-\xF4][\x80-\xBF]*");
            lua_setfield(state, -2, "charpattern");

            lua_setglobal(state, "utf8");
        }

        void sandbox(lua_State *state) {
            luaL_openlibs(state);

            setGlobalNil(state, "io");
            setGlobalNil(state, "os");
            setGlobalNil(state, "debug");
            setGlobalNil(state, "package");
            setGlobalNil(state, "require");
            setGlobalNil(state, "module");
            setGlobalNil(state, "dofile");
            setGlobalNil(state, "loadfile");
        }

    }

    class LuaExecutionEnvironment::Private {
    public:
        explicit Private(lua_State *state) : state(state) {
            if (state) {
                lua_pushlightuserdata(state, &interruptFlagRegistryKey);
                lua_pushlightuserdata(state, &interrupted);
                lua_settable(state, LUA_REGISTRYINDEX);
            }
        }

        ~Private() {
            if (state) {
                lua_close(state);
            }
        }

        lua_State *state{};
        std::atomic_bool interrupted{false};
    };

    LuaExecutionEnvironment::LuaExecutionEnvironment() : d(std::make_unique<Private>(luaL_newstate())) {
        if (!d->state) {
            throw std::runtime_error("failed to create Lua state");
        }
        sandbox(d->state);
        registerUtf8(d->state);
    }

    LuaExecutionEnvironment::~LuaExecutionEnvironment() = default;

    LuaExecutionEnvironment::LuaExecutionEnvironment(LuaExecutionEnvironment &&) noexcept = default;

    LuaExecutionEnvironment &LuaExecutionEnvironment::operator=(LuaExecutionEnvironment &&) noexcept = default;

    lua_State *LuaExecutionEnvironment::state() const {
        return d->state;
    }

    void LuaExecutionEnvironment::interrupt() noexcept {
        d->interrupted.store(true, std::memory_order_release);
    }

    int LuaExecutionEnvironment::pcall(int nargs, int nresults, int msgh) {
        lua_sethook(d->state, interruptHook, LUA_MASKCOUNT, LuaScript::interruptHookCountInterval());
        const auto result = lua_pcall(d->state, nargs, nresults, msgh);
        lua_sethook(d->state, nullptr, 0, 0);
        d->interrupted.store(false, std::memory_order_release);
        return result;
    }

    void LuaExecutionEnvironment::loadAndRun(const LuaScript &script) {
        const auto &byteCode = script.byteCode();
        if (luaL_loadbuffer(d->state, reinterpret_cast<const char *>(byteCode.data()), byteCode.size(), script.chunkName().c_str()) != 0) {
            const auto message = errorString();
            lua_pop(d->state, 1);
            throw std::runtime_error("failed to load Lua bytecode: " + message);
        }

        lua_newtable(d->state);
        lua_pushlstring(d->state, script.chunkName().data(), script.chunkName().size());
        lua_setfield(d->state, -2, "chunkName");
        lua_pushcclosure(d->state, sandboxPrint, 1);
        lua_setglobal(d->state, "print");

        if (pcall(0, 0, 0) != 0) {
            const auto message = errorString();
            lua_pop(d->state, 1);
            throw std::runtime_error("failed to execute Lua chunk: " + message);
        }
    }

    int LuaExecutionEnvironment::getGlobalFunctionRef(const std::string &name) {
        lua_getglobal(d->state, name.c_str());
        if (!lua_isfunction(d->state, -1)) {
            lua_pop(d->state, 1);
            throw std::runtime_error("global '" + name + "' is not a function");
        }
        return luaL_ref(d->state, LUA_REGISTRYINDEX);
    }

    void LuaExecutionEnvironment::pushRef(int ref) const {
        lua_rawgeti(d->state, LUA_REGISTRYINDEX, ref);
    }

    void LuaExecutionEnvironment::releaseRef(int ref) {
        luaL_unref(d->state, LUA_REGISTRYINDEX, ref);
    }

    std::string LuaExecutionEnvironment::errorString(int index) const {
        const auto *message = lua_tostring(d->state, index);
        if (!message) {
            return {};
        }
        return message;
    }

}
