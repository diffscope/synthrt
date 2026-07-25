# Audio 单元测试
#
# tst_audio_skeleton.cpp 是保留的骨架测试（仅 REQUIRE(true)），保证 target
# 即使在没有 GLOB 匹配文件时也能注册。
synthrt_add_unittest(synthrt-unittest-audio tst_audio_skeleton.cpp)

# 额外的 Audio 单元测试：通过 GLOB 自动纳入 tst_*.cpp。
file(GLOB _audio_tests CONFIGURE_DEPENDS "tst_*.cpp")
if(_audio_tests)
    synthrt_add_unittest(synthrt-unittest-audio-ext "${_audio_tests}")
    target_link_libraries(synthrt-unittest-audio-ext PRIVATE srt::audio)
endif()
