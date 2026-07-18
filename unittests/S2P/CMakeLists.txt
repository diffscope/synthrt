# S2P 单元测试
#
# test_s2p_strategies.cpp 覆盖 DirectS2P / DictionaryS2P / MappingS2P /
# LanguageResource 的边缘场景，不依赖 LuaJIT，始终参与编译。
# test_lua_disabled.cpp 仅在 LuaJIT 禁用时编译，验证 Lua API 报告
# FeatureNotSupported。

synthrt_add_unittest(synthrt-unittest-s2p test_s2p_strategies.cpp)
target_link_libraries(synthrt-unittest-s2p PRIVATE srt::s2p)

if(NOT SYNTHRT_ENABLE_LUAJIT)
    synthrt_add_unittest(synthrt-unittest-s2p-lua test_lua_disabled.cpp)
    target_link_libraries(synthrt-unittest-s2p-lua PRIVATE srt::s2p)
endif()
