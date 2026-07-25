file(GLOB _c_tests CONFIGURE_DEPENDS "tst_*.cpp")
if(_c_tests)
    synthrt_add_unittest(synthrt-unittest-c "${_c_tests}")

    target_include_directories(synthrt-unittest-c PRIVATE
        ${SYNTHRT_SOURCE_DIR}/lib/C
    )
    target_link_libraries(synthrt-unittest-c PRIVATE srt::c)
endif()
