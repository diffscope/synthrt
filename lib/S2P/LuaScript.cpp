#include <synthrt/S2P/LuaScript.h>

#include "internal/LuaExecutionEnvironment.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <lua.hpp>

namespace srt::s2p {

    class LuaScript::Private {
    public:
        std::string chunkName;
        std::vector<unsigned char> byteCode;
    };

    namespace {

        LuaScript::PrintHandler &printHandler() {
            static LuaScript::PrintHandler handler;
            return handler;
        }

        int &interruptHookCountIntervalStorage() {
            static int interval = 65536;
            return interval;
        }

        struct LuaStateDeleter {
            void operator()(lua_State *state) const {
                if (state) {
                    lua_close(state);
                }
            }
        };

        struct DumpContext {
            std::vector<unsigned char> *byteCode{};
            bool failed{};
        };

        int writeByteCode(lua_State *, const void *data, std::size_t size, void *userData) {
            auto *context = static_cast<DumpContext *>(userData);
            try {
                const auto *bytes = static_cast<const unsigned char *>(data);
                context->byteCode->insert(context->byteCode->end(), bytes, bytes + size);
            } catch (...) {
                context->failed = true;
                return 1;
            }
            return 0;
        }

        std::string luaError(lua_State *state) {
            const auto *message = lua_tostring(state, -1);
            if (!message) {
                return {};
            }
            return message;
        }

    }

    LuaScript::LuaScript() : d(std::make_unique<Private>()) {
    }

    LuaScript::~LuaScript() = default;

    LuaScript::LuaScript(LuaScript &&) noexcept = default;

    LuaScript &LuaScript::operator=(LuaScript &&) noexcept = default;

    srt::core::Expected<std::unique_ptr<LuaScript>>
    LuaScript::create(const std::string &script, const std::string &chunkName) {
        auto obj = std::unique_ptr<LuaScript>(new LuaScript());
        obj->d->chunkName = chunkName;

        const std::unique_ptr<lua_State, LuaStateDeleter> state(luaL_newstate());
        if (!state) {
            return srt::core::Error(srt::core::ErrorCode::S2pScriptError,
                "LuaScript error: failed to create Lua state");
        }

        if (luaL_loadbuffer(state.get(), script.data(), script.size(), chunkName.c_str()) != 0) {
            auto message = luaError(state.get());
            if (message.empty()) {
                message = "unknown compile error";
            }
            return srt::core::Error(srt::core::ErrorCode::S2pScriptError,
                "LuaScript compile error: " + message);
        }

        DumpContext context{&obj->d->byteCode, false};
        if (lua_dump(state.get(), writeByteCode, &context) != 0 || context.failed) {
            obj->d->byteCode.clear();
            return srt::core::Error(srt::core::ErrorCode::S2pScriptError,
                "LuaScript dump error: failed to dump bytecode");
        }

        return obj;
    }

    void LuaScript::setLuaPrintHandler(PrintHandler handler) {
        printHandler() = std::move(handler);
    }

    LuaScript::PrintHandler LuaScript::luaPrintHandler() {
        return printHandler();
    }

    void LuaScript::setInterruptHookCountInterval(int interval) {
        interruptHookCountIntervalStorage() = interval;
    }

    int LuaScript::interruptHookCountInterval() {
        return interruptHookCountIntervalStorage();
    }

    const std::vector<unsigned char> &LuaScript::byteCode() const {
        return d->byteCode;
    }

    const std::string &LuaScript::chunkName() const {
        return d->chunkName;
    }

    namespace Internal {

        void dispatchLuaPrint(const std::string &chunkName, const std::vector<std::string> &arguments) {
            const auto handler = LuaScript::luaPrintHandler();
            if (handler) {
                handler(chunkName, arguments);
            }
        }

    }

}
