cmake_minimum_required(VERSION 3.19)
find_program(_tidy NAMES clang-tidy REQUIRED)
get_filename_component(_policy "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED TEST_DIR)
    set(TEST_DIR "${_policy}/../../.cache/cpp-rules-tests")
endif()
file(MAKE_DIRECTORY "${TEST_DIR}")
get_filename_component(TEST_DIR "${TEST_DIR}" ABSOLUTE)

# Each rejected example must produce its expected diagnostic and a nonzero
# exit status. A compiler error cannot substitute for a naming diagnostic.
function(check_case name source diagnostic)
    file(WRITE "${TEST_DIR}/${name}.cpp" "${source}")
    execute_process(COMMAND "${_tidy}" "${TEST_DIR}/${name}.cpp"
        "--config-file=${_policy}/.clang-tidy" "--header-filter=.*" -- -std=c++17
        RESULT_VARIABLE _status OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    set(_output "${_out}${_err}")
    if(_output MATCHES "clang-diagnostic-error")
        message(FATAL_ERROR "${name}: invalid fixture\n${_output}")
    endif()
    if(diagnostic STREQUAL "")
        if(NOT _status EQUAL 0 OR _output MATCHES "readability-identifier-naming")
            message(FATAL_ERROR "${name}: unexpected diagnostic\n${_output}")
        endif()
    elseif(_status EQUAL 0 OR NOT _output MATCHES "${diagnostic}")
        message(FATAL_ERROR "${name}: missing rejection ${diagnostic}\n${_output}")
    endif()
    math(EXPR _count "${_count} + 1")
    set(_count ${_count} PARENT_SCOPE)
endfunction()

set(_count 0)
check_case(Accepted [=[
namespace example {
constexpr int API_LEVEL = 1;
int g_counter = 0;
static int s_counter = 0;
struct PublicValue { int value; };
enum class State { Ready, Failed };
using ValueType = int;
class Sample {
public:
    int value() const { return m_value; }
private:
    int m_value = 0;
    void *_impl = nullptr;
    void *_decl = nullptr;
};
int add(int LHS, int RHS) { int result = LHS + RHS; return result; }
}
]=] "")
check_case(Class "class bad_class {};" "invalid case style for class 'bad_class'")
check_case(Function "void BadFunction() {}" "invalid case style for function 'BadFunction'")
check_case(Member "class Sample { int value; };" "invalid case style for private member 'value'")
check_case(Constant "constexpr int badConstant = 1;" "invalid case style for constant 'badConstant'")
check_case(Parameter "void accept(int BadParameter) {}" "invalid case style for parameter 'BadParameter'")
check_case(Enum "enum class State { bad_value };" "invalid case style for enum constant 'bad_value'")
check_case(Global "int counter = 0;" "invalid case style for global variable 'counter'")
file(WRITE "${TEST_DIR}/Owned.h" "struct bad_header_type {};\n")
check_case(Header "#include \"Owned.h\"\n" "invalid case style for struct 'bad_header_type'")
check_case(Macro "#define DECLARE_TYPE(name) struct name {};\nDECLARE_TYPE(bad_macro_type)\n"
    "invalid case style for struct 'bad_macro_type'")
message(STATUS "${_count} naming cases passed, including owned headers and macro arguments")

# Exercise the public runner as a consumer would, including failure propagation.
find_program(_format NAMES clang-format REQUIRED)
set(_repo "${TEST_DIR}/consumer")
file(MAKE_DIRECTORY "${_repo}")
configure_file("${_policy}/.clang-format" "${_repo}/.clang-format" COPYONLY)
file(WRITE "${_repo}/Good.h" "struct Good {};\n")
execute_process(COMMAND "${_format}" -i "--style=file:${_policy}/.clang-format" "${_repo}/Good.h"
    RESULT_VARIABLE _status)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "Could not format runner fixture")
endif()
function(check_runner name expected)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DREPO_ROOT=${_repo}" ${ARGN}
        -P "${_policy}/Check.cmake" RESULT_VARIABLE _status
        OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(expected STREQUAL "pass")
        if(NOT _status EQUAL 0)
            message(FATAL_ERROR "${name}: unexpected failure\n${_out}${_err}")
        endif()
    elseif(_status EQUAL 0 OR NOT "${_out}${_err}" MATCHES "${expected}")
        message(FATAL_ERROR "${name}: missing expected failure ${expected}\n${_out}${_err}")
    endif()
endfunction()
check_runner(FormattedHeader pass "-DFILES=Good.h")
file(WRITE "${_repo}/Bad.h" "struct Bad{int value;};\n")
check_runner(FormatFailure "Format check failed" "-DFILES=Bad.h")
file(WRITE "${_repo}/.clang-format" "BasedOnStyle: LLVM\n")
check_runner(ConfigDrift "Format configuration differs")
check_runner(ConfigSync pass -DSYNC_FORMAT=ON)
file(WRITE "${_repo}/Good.cpp" "int main() {}\n")
execute_process(COMMAND "${_format}" -i "--style=file:${_policy}/.clang-format" "${_repo}/Good.cpp"
    RESULT_VARIABLE _status)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "Could not format translation unit fixture")
endif()
check_runner(MissingDatabase "Missing compilation database" "-DFILES=Good.cpp"
    "-DBUILD_DIR=${_repo}/missing")
message(STATUS "5 runner cases passed, including format failures, config drift and missing database")
