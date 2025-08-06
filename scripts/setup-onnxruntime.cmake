# Usage (replace <this> with the path of this script)
# Regular download:
#   cmake -P <this>
# With CUDA 11.x support:
#   cmake -Dep=cuda11 -P <this>
# With CUDA 12.x support:
#   cmake -Dep=cuda12 -P <this>

function(get_windows_proxy _out)
    execute_process(
        COMMAND reg query "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings" /v ProxyEnable
        OUTPUT_VARIABLE _proxy_enable_output
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(NOT _proxy_enable_output MATCHES "ProxyEnable[ \t\r\n]+REG_DWORD[ \t\r\n]+0x1")
        return()
    endif()

    execute_process(
        COMMAND reg query "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings" /v ProxyServer
        OUTPUT_VARIABLE _proxy_server_output
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(NOT _proxy_server_output MATCHES "ProxyServer[ \t\r\n]+REG_SZ[ \t\r\n]+(.*)")
        return()
    endif()

    set(${_out} ${CMAKE_MATCH_1} PARENT_SCOPE)
endfunction()

# Detect system proxy
if(WIN32)
    set(_proxy)
    get_windows_proxy(_proxy)

    if(_proxy)
        set(ENV{HTTP_PROXY} "http://${_proxy}")
        set(ENV{HTTPS_PROXY} "http://${_proxy}")
        set(ENV{ALL_PROXY} "http://${_proxy}")
        message(STATUS "Use system proxy: ${_proxy}")
    endif()
endif()

# Download OnnxRuntime Release
set(_os)
set(_ext "tgz")

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(_os "win")
    set(_ext "zip")
    set(_os_display_name "Windows")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(_os "linux")
    set(_os_display_name "Linux")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(_os "osx")
    set(_os_display_name "macOS")
else()
    message(FATAL_ERROR "Unsupported operating system: ${CMAKE_SYSTEM_NAME}")
endif()

if(NOT DEFINED CMAKE_HOST_SYSTEM_PROCESSOR)
    if(WIN32)
        set(_win_arch $ENV{PROCESSOR_ARCHITECTURE})

        if(_win_arch STREQUAL "AMD64")
            set(_detected_arch "x64")
        elseif(_win_arch STREQUAL "x86")
            set(_detected_arch "x86")
        elseif(_win_arch STREQUAL "ARM64")
            set(_detected_arch "ARM64")
        endif()
    elseif(APPLE)
        execute_process(COMMAND uname -m OUTPUT_VARIABLE _detected_arch OUTPUT_STRIP_TRAILING_WHITESPACE)

        if(_detected_arch STREQUAL "x86_64")
            execute_process(COMMAND sysctl -n sysctl.proc_translated OUTPUT_VARIABLE _proc_translated OUTPUT_STRIP_TRAILING_WHITESPACE)

            if(_proc_translated STREQUAL "1")
                set(_detected_arch "arm64")
            endif()
        endif()
    else()

        execute_process(COMMAND uname -m OUTPUT_VARIABLE _detected_arch OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_detected_arch STREQUAL "x86_64")
            set(_detected_arch "x64")
        else()
            message(FATAL_ERROR "Unsupported Architecture: Linux-${_detected_arch}")
        endif()
    endif()
endif()

set(_arch ${_detected_arch})

set(_version_ort "1.17.3")
set(_version_dml "1.15.4")

# DirectML v1.15.4 NuGet release binaries
# https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/1.15.4
set(SHA512_Microsoft_AI_DirectML_1_15_4_zip
        "fde767f56904abc90fd53f65d8729c918ab7f6e3c5e1ecdd479908fc02b4535cf2b0860f7ab2acb9b731d6cb809b72c3d5d4d02853fb8f5ea022a47bc44ef285")

# ONNX Runtime v1.17.3 NuGet release binaries
# https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/1.17.3
set(SHA512_Microsoft_ML_OnnxRuntime_DirectML_1_17_3_zip
        "570eae618edb36eb7f7a5049c9c78d4162409c1907039b284824080777af9da149b383c9c8afe970a0617e7169e5681b7f0d33445eaaddebd2b38e7c9744d8ba")

# ONNX Runtime v1.17.3 GitHub release binaries
# https://github.com/microsoft/onnxruntime/releases/tag/v1.17.3
set(SHA512_onnxruntime_win_arm64_1_17_3_zip
        "8fb1f70f06210a5f93ee315138352b88f8164ab1f399e714e4d37d131f86bf737680fe27591d844aaca468bf5a9cbc980d86ad7a88ff941bb6ba9ecbc275420e")
set(SHA512_onnxruntime_win_x64_1_17_3_zip
        "d9f7c21b0e4ee64e84923904e05d04686231ab9240724dad7e7efc05d890d73404d92e9d07f14d14507d897da846a754b474b7b036e8416a06daaf200e1ec488")
set(SHA512_onnxruntime_win_x64_gpu_1_17_3_zip
        "09f702128f6a9b22b2d5b56e998264eb1f18fcf0fab1767703bcd327bb40325e4d52eab30f4c94379dda304a4c7807a7bb2ad91bbf1148483ecba1ad54a56ef7")
set(SHA512_onnxruntime_win_x64_gpu_cuda12_1_17_3_zip
        "81fbcb2ca5f245ee320af83ebdd3a5505a23e44d6bd32c47af00972a224ed0d410eef16f592bc9616943443361547128f8ffd8edf3a076708908004aef6225a4")
set(SHA512_onnxruntime_linux_aarch64_1_17_3_tgz
        "cb07453a46354f8dfd1e5889ebc6b9c52dfa31cb301b21a1033e7ca3156a5a9ca5dd86d17ec3e573e425a841704f69feab9267466e44b66ba35a6edbc912a1c9")
set(SHA512_onnxruntime_linux_x64_1_17_3_tgz
        "c13273acb7730f0f5eed569cff479d34c9674f5f39d2a76a2c960835560e9706fd92e07071dd66fe242738c31f0df19d830b7e5083378c9e0657685727725ca0")
set(SHA512_onnxruntime_linux_x64_gpu_1_17_3_tgz
        "4f2d760d7a2e4ec936844a2a24003df1b1744b951ebe94edc05630fd91e6f0816bfa98e31119d2caf00a3f38bdccf83d2eac200cfc70a760f4946c81df5dae90")
set(SHA512_onnxruntime_linux_x64_gpu_cuda12_1_17_3_tgz
        "3f610489a25abecaf9e53b903ed1e84215bbf57a2cb46b88ab5434dd8a63caf5b168b4c0646b0b81a78ea574301ebcbd9b32fd6641844ca2b374c36485f6d098")
set(SHA512_onnxruntime_linux_x64_rocm_1_17_3_tgz
        "be63c128578f6bb66ed0fb74f40376636847903c57b21b79ec6f9a37144994a6d26e69adceb1f19639e36be37097fca9230e7bf496ba4dda666ac6053fb9ee96")
set(SHA512_onnxruntime_osx_arm64_1_17_3_tgz
        "1e002f8d2d89cb99d2bd9c2c61ef7cfe4e72724f21a6a3d5df6524f92cc9dd5096754e871b2ee7e5588d5f09a320f5eb0f484a95ff70d4b05990dfa388c344bf")
set(SHA512_onnxruntime_osx_universal2_1_17_3_tgz
        "9ef9bc7d30a2dc899ceab269f48d650427f2bb78d6a20c2245bd993e14977e0b247222776e0f4486acfa3679beac3c1c45973e9c866ae886a6d8f127caeeeca6")
set(SHA512_onnxruntime_osx_x86_64_1_17_3_tgz
        "175712dccb8d57cf4f0e7668f3e7ed42329ace19c54f3a5670e8cf13a335faf90889b6c248e855ab3d8ebb1254c6484fe91f7bb732f959816c02463c6b9a9626")

function(_make_safe_varname filename out_var)
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" safe_name "${filename}")
    set(${out_var} "${safe_name}" PARENT_SCOPE)
endfunction()

function(_lookup_sha512 filename out_hash)
    _make_safe_varname("${filename}" safe_var)
    set(var_name "SHA512_${safe_var}")
    if(DEFINED ${var_name})
        set(${out_hash} "${${var_name}}" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unknown hash for: ${filename}")
    endif()
endfunction()

macro(download_onnxruntime_from_github)
    set(_base_url "https://github.com/microsoft/onnxruntime/releases/download/v${_version_ort}")
    set(_name      "onnxruntime-${_os}-${_arch}-${_full_version}")
    set(_name_zip  "onnxruntime-${_os}-${_arch}-${_full_version_zip}")
    set(_name_zip_ort "${_name}.${_ext}")
    set(_url       "${_base_url}/${_name_zip_ort}")
    set(_file_path "${CMAKE_BINARY_DIR}/${_name_zip_ort}")
    _lookup_sha512("${_name_zip_ort}" _expected_hash)

    message(STATUS "Downloading ONNX Runtime from ${_url}")

    file(DOWNLOAD ${_url} ${_file_path}
        SHOW_PROGRESS
        EXPECTED_HASH SHA512=${_expected_hash}
        TLS_VERIFY ON
    )

    if (${ARGC} GREATER 0)
        set(_extract_dir ${CMAKE_BINARY_DIR}/onnxruntime/${ARGV0})
    else()
        set(_extract_dir ${CMAKE_BINARY_DIR}/onnxruntime)
    endif()

    file(ARCHIVE_EXTRACT INPUT ${_file_path}
            DESTINATION ${_extract_dir}
    )
    file(REMOVE ${_file_path})
    file(MAKE_DIRECTORY ${_extract_dir}/${_name_zip}/include)
    file(MAKE_DIRECTORY ${_extract_dir}/${_name_zip}/lib)
    file(COPY ${_extract_dir}/${_name_zip}/include DESTINATION ${_extract_dir})
    file(COPY ${_extract_dir}/${_name_zip}/lib DESTINATION ${_extract_dir})
    file(REMOVE_RECURSE ${_extract_dir}/${_name_zip})
endmacro()

function(copy_contents _src _dst)
    file(GLOB SOURCE_FILES "${_src}/*")
    file(MAKE_DIRECTORY ${_dst})
    foreach(FILE ${SOURCE_FILES})
        file(COPY ${FILE} DESTINATION ${_dst})
    endforeach()
endfunction()

macro(download_onnxruntime_from_nuget)
    set(_url_ort "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/${_version_ort}")
    set(_name_zip_ort "Microsoft.ML.OnnxRuntime.DirectML.${_version_ort}.zip")
    set(_file_path_ort "${CMAKE_BINARY_DIR}/${_name_zip_ort}")
    _lookup_sha512("${_name_zip_ort}" _expected_hash)
    message(STATUS "Downloading ONNX Runtime from ${_url_ort}")

    file(DOWNLOAD ${_url_ort} ${_file_path_ort}
        SHOW_PROGRESS
        EXPECTED_HASH SHA512=${_expected_hash}
        TLS_VERIFY ON
    )

    if(${ARGC} GREATER 0)
        set(_extract_dir ${CMAKE_BINARY_DIR}/onnxruntime/${ARGV0})
    else()
        set(_extract_dir ${CMAKE_BINARY_DIR}/onnxruntime)
    endif()
    set(_extract_dir_ort ${CMAKE_BINARY_DIR}/nuget_onnxruntime)

    file(MAKE_DIRECTORY ${_extract_dir_ort})

    file(ARCHIVE_EXTRACT INPUT ${_file_path_ort}
            DESTINATION ${_extract_dir_ort}
    )
    file(REMOVE ${_file_path_ort})

    file(COPY ${_extract_dir_ort}/build/native/include DESTINATION ${_extract_dir})
    file(MAKE_DIRECTORY "${extract_dir}/lib")
    copy_contents("${_extract_dir_ort}/runtimes/win-x64/native" "${_extract_dir}/lib")

    file(REMOVE_RECURSE ${_extract_dir_ort})
endmacro()

macro(download_dml_from_nuget)
    set(_url_dml "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/${_version_dml}")
    set(_name_zip_dml "Microsoft.AI.DirectML.${_version_dml}.zip")
    set(_file_path_dml "${CMAKE_BINARY_DIR}/${_name_zip_dml}")
    _lookup_sha512("${_name_zip_dml}" _expected_hash)
    message(STATUS "Downloading DirectML from ${_url_dml}")

    file(DOWNLOAD ${_url_dml} ${_file_path_dml}
        SHOW_PROGRESS
        EXPECTED_HASH SHA512=${_expected_hash}
        TLS_VERIFY ON
    )

    if(${ARGC} GREATER 0)
        set(_extract_dir ${CMAKE_BINARY_DIR}/onnxruntime/${ARGV0})
    else()
        set(_extract_dir ${CMAKE_BINARY_DIR}/onnxruntime)
    endif()
    set(_extract_dir_dml ${CMAKE_BINARY_DIR}/nuget_directml)

    file(MAKE_DIRECTORY ${_extract_dir_dml})

    file(ARCHIVE_EXTRACT INPUT ${_file_path_dml}
            DESTINATION ${_extract_dir_dml}
    )
    file(REMOVE ${_file_path_dml})

    file(COPY ${_extract_dir_dml}/include DESTINATION ${_extract_dir})
    file(MAKE_DIRECTORY "${extract_dir}/lib")
    copy_contents("${_extract_dir_dml}/bin/x64-win" "${_extract_dir}/lib")

    file(REMOVE_RECURSE ${_extract_dir_dml})
endmacro()

if(DEFINED ep)
    string(TOLOWER "${ep}" ep)
endif()

#if(DEFINED ep AND "${ep}" STREQUAL "gpu")
#    message("Selected Execution Provider: GPU (CUDA)")
#    set(_full_version      gpu-${_version_ort})
#    set(_full_version_zip  gpu-${_version_ort})
#    download_onnxruntime_from_github()
#elseif(DEFINED ep AND "${ep}" STREQUAL "gpu-cuda12")
#    message("Selected Execution Provider: GPU (CUDA12)")
#    set(_full_version      gpu-cuda12-${_version_ort})
#    set(_full_version_zip  gpu-${_version_ort})
#    download_onnxruntime_from_github()
#elseif(DEFINED ep AND ("${ep}" STREQUAL "dml" OR "${ep}" STREQUAL "directml"))
#    message("Selected Execution Provider: DirectML")
#    download_onnxruntime_from_nuget()
#    download_dml_from_nuget()
#else()
#    message("Selected Execution Provider: CPU")
#    set(_full_version      ${_version_ort})
#    set(_full_version_zip  ${_version_ort})
#    download_onnxruntime_from_github()
#endif()


# First, download default version of ONNX Runtime (Windows: DirectML, other OS: CPU)
message(STATUS "OS: ${_os_display_name}")
if(WIN32)
    message(STATUS "Downloading DirectML version of ONNX Runtime...")
    set(_deploy_subdir "default")
    download_onnxruntime_from_nuget(${_deploy_subdir})
    download_dml_from_nuget(${_deploy_subdir})
else()
    message(STATUS "Downloading CPU version of ONNX Runtime...")
    set(_deploy_subdir "default")
    set(_full_version      ${_version_ort})
    set(_full_version_zip  ${_version_ort})
    download_onnxruntime_from_github(${_deploy_subdir})
endif()

# GPU (CUDA) version: optional download
if (DEFINED ep AND ("${ep}" STREQUAL "cuda11" OR "${ep}" STREQUAL "cuda" OR "${ep}" STREQUAL "gpu"))
    message(STATUS "Downloading GPU (CUDA 11.x) version of ONNX Runtime...")
    set(_deploy_subdir "cuda")
    set(_full_version      gpu-${_version_ort})
    set(_full_version_zip  gpu-${_version_ort})
    download_onnxruntime_from_github(${_deploy_subdir})
elseif(DEFINED ep AND ("${ep}" STREQUAL "cuda12" OR "${ep}" STREQUAL "gpu-cuda12"))
    message(STATUS "downloading GPU (CUDA 12.x) version of ONNX Runtime...")
    set(_deploy_subdir "cuda")
    set(_full_version      gpu-cuda12-${_version_ort})
    set(_full_version_zip  gpu-${_version_ort})
    download_onnxruntime_from_github(${_deploy_subdir})
endif()
