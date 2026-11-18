execute_process(
    COMMAND ${CMAKE_COMMAND} -E env
        "SYNTHRT_TEST_FATAL_LIFETIME=${VARIANT}"
        "${EXECUTABLE}" --run_test=executive_lifetime_violation_is_fatal
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

string(CONCAT combined "${output}" "${error}")
if(result EQUAL 0)
    message(FATAL_ERROR "the lifetime violation exited successfully")
endif()
string(FIND "${combined}" "${EXPECTED}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "the fatal diagnostic did not contain ${EXPECTED}: ${combined}")
endif()
