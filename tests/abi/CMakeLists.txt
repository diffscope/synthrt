synthrt_add_cross_test(synthrt-test-c-abi tst_c_abi_skeleton.cpp)

# WP7: vnext session/model/task handle-table tests.
# Links srt::c for the C ABI entry points and adds lib/C + include/ to the
# include path so the test can access the internal HandleTable.h / LastError.h
# headers and the public <synthrt/C/srt.h>.
synthrt_add_cross_test(synthrt-test-session-handle tst_session_handle.cpp)
target_link_libraries(synthrt-test-session-handle PRIVATE srt::c)
target_include_directories(synthrt-test-session-handle PRIVATE
    ${SYNTHRT_SOURCE_DIR}/lib/C
    ${SYNTHRT_SOURCE_DIR}/include
)
