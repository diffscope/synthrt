#include <string>

#include <catch2/catch_test_macros.hpp>

#include <synthrt/S2P/LuaOnsetMarker.h>
#include <synthrt/S2P/LuaS2P.h>
#include <synthrt/S2P/LuaScript.h>

TEST_CASE("Lua API reports unsupported when LuaJIT is disabled", "[s2p][lua]") {
    auto script = srt::s2p::LuaScript::create("return true", "disabled-test");

    REQUIRE_FALSE(script);
    REQUIRE(script.errorCode() == srt::core::ErrorCode::FeatureNotSupported);
    REQUIRE(script.errorMessage().find("without LuaJIT support") != std::string::npos);

    REQUIRE(&srt::s2p::LuaS2P::create != nullptr);
    REQUIRE(&srt::s2p::LuaS2P::convert != nullptr);
    REQUIRE(&srt::s2p::LuaOnsetMarker::create != nullptr);
    REQUIRE(&srt::s2p::LuaOnsetMarker::mark != nullptr);
}
