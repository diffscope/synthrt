# Detect OS and archive extension
if(VCPKG_TARGET_IS_WINDOWS)
    set(_os "win")
    set(_ext "zip")
elseif(VCPKG_TARGET_IS_LINUX)
    set(_os "linux")
    set(_ext "tgz")
elseif(VCPKG_TARGET_IS_OSX)
    set(_os "osx")
    set(_ext "tgz")
else()
    message(FATAL_ERROR "Unsupported operating system: ${VCPKG_TARGET_TRIPLET}")
endif()

if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    if(VCPKG_TARGET_IS_OSX)
        set(_arch "x86_64")
    else()
        set(_arch "x64")
    endif()
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
    set(_arch "x86")
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    if(VCPKG_TARGET_IS_LINUX)
        set(_arch "aarch64")
    else()
        set(_arch "arm64")
    endif()
else()
    message(FATAL_ERROR "Unsupported architecture: ${VCPKG_TARGET_ARCHITECTURE}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/versions.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/hashes.cmake")

# Deploy one native payload directory, whitelist-split by filename pattern
# into three physically separated destination trees (layout decision D7,
# docs/audit-report-onnxruntime-builds.md):
#   *.{dll,dylib,so,so.*}  runtime binaries → <runtime_dst> (pure-DLL tree;
#                          consumers glob it for deployment, symbols can
#                          never leak into shipped packages)
#   *.pdb / *.dSYM         debug symbols → <pdb_dst> (parallel tree, same
#                          flavor structure; consumers copy them on demand
#                          for Debug/RelWithDebInfo staging)
#   *.lib                  import libraries → <import_dst> (per-flavor
#                          private; kills the DML/GPU onnxruntime.lib
#                          overwrite; nothing links them today, kept as
#                          stock)
#   everything else        NOT deployed: upstream lib/cmake configs and
#                          pkgconfig embed dead upstream paths — junk in the
#                          vcpkg layout (and would cross-flavor overwrite too)
function(_deploy_runtime_contents _src _runtime_dst _pdb_dst _import_dst)
    file(GLOB SOURCE_FILES "${_src}/*")
    file(MAKE_DIRECTORY "${_runtime_dst}")
    foreach(FILE IN LISTS SOURCE_FILES)
        get_filename_component(_file_name "${FILE}" NAME)
        if(_file_name MATCHES "\\.dll$|\\.dylib$|\\.so(\\..*)?$")
            file(COPY "${FILE}" DESTINATION "${_runtime_dst}")
        elseif(_file_name MATCHES "\\.pdb$|\\.dSYM$")
            file(MAKE_DIRECTORY "${_pdb_dst}")
            file(COPY "${FILE}" DESTINATION "${_pdb_dst}")
        elseif(_file_name MATCHES "\\.lib$")
            file(MAKE_DIRECTORY "${_import_dst}")
            file(COPY "${FILE}" DESTINATION "${_import_dst}")
        endif()
    endforeach()
endfunction()

function(download_onnxruntime_from_github _ep _deploy_dst_dir _runtime_dst_dir _pdb_dst_dir _import_dst_dir)
    string(TOLOWER "${_ep}" _ep)
    if(_ep STREQUAL "cpu")
        set(_full_version "${_version_ort}")
    else()
        # All official GPU assets are a single CUDA-12 build; the cudaXX flavor
        # distinction is enforced at the port level, not by upstream naming.
        set(_full_version "gpu-${_version_ort}")
    endif()

    set(_base_url "https://github.com/microsoft/onnxruntime/releases/download/v${_version_ort}")
    set(_name "onnxruntime-${_os}-${_arch}-${_full_version}")
    set(_download_filename "${_name}.${_ext}")
    set(_url "${_base_url}/${_download_filename}")

    message(STATUS "Downloading ONNX Runtime from ${_url}")

    lookup_onnxruntime_package_sha512("${_download_filename}" _file_hash)
    vcpkg_download_distfile(
            _downloaded_file_path
            URLS "${_url}"
            FILENAME "${_download_filename}"
            SHA512 "${_file_hash}"
    )

    set(_extract_dir "${CURRENT_BUILDTREES_DIR}/tmp_onnxruntime")

    file(REMOVE_RECURSE "${_extract_dir}")

    vcpkg_extract_archive(
            ARCHIVE "${_downloaded_file_path}"
            DESTINATION "${_extract_dir}"
    )

    file(COPY "${_extract_dir}/${_name}/include" DESTINATION "${_deploy_dst_dir}")
    _deploy_runtime_contents("${_extract_dir}/${_name}/lib" "${_runtime_dst_dir}" "${_pdb_dst_dir}" "${_import_dst_dir}")
    file(REMOVE_RECURSE "${_extract_dir}")
endfunction()

function(download_onnxruntime_from_nuget _deploy_dst_dir _runtime_dst_dir _pdb_dst_dir _import_dst_dir)
    set(_url "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/${_version_ort_dml}")
    set(_download_filename "Microsoft.ML.OnnxRuntime.DirectML.${_version_ort_dml}.zip")

    message(STATUS "Downloading ONNX Runtime from ${_url}")

    lookup_onnxruntime_package_sha512("${_download_filename}" _file_hash)
    vcpkg_download_distfile(
            _downloaded_file_path_ort
            URLS "${_url}"
            FILENAME "${_download_filename}"
            SHA512 "${_file_hash}"
    )

    set(_extract_dir "${CURRENT_BUILDTREES_DIR}/tmp_onnxruntime")

    file(REMOVE_RECURSE "${_extract_dir}")

    vcpkg_extract_archive(
        ARCHIVE "${_downloaded_file_path_ort}"
        DESTINATION "${_extract_dir}"
    )

    file(COPY "${_extract_dir}/build/native/include" DESTINATION "${_deploy_dst_dir}")

    _deploy_runtime_contents("${_extract_dir}/runtimes/win-x64/native" "${_runtime_dst_dir}" "${_pdb_dst_dir}" "${_import_dst_dir}")

    file(REMOVE_RECURSE "${_extract_dir}")
endfunction()

function(download_dml_from_nuget _deploy_dst_dir _runtime_dst_dir _pdb_dst_dir _import_dst_dir)
    set(_url "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/${_version_dml}")
    set(_download_filename "Microsoft.AI.DirectML.${_version_dml}.zip")

    message(STATUS "Downloading DirectML from ${_url}")

    lookup_onnxruntime_package_sha512("${_download_filename}" _file_hash)
    vcpkg_download_distfile(
            _downloaded_file_path_dml
            URLS "${_url}"
            FILENAME "${_download_filename}"
            SHA512 "${_file_hash}"
    )

    set(_extract_dir "${CURRENT_BUILDTREES_DIR}/tmp_directml")

    file(REMOVE_RECURSE "${_extract_dir}")

    vcpkg_extract_archive(
        ARCHIVE "${_downloaded_file_path_dml}"
        DESTINATION "${_extract_dir}"
    )

    file(COPY "${_extract_dir}/include" DESTINATION "${_deploy_dst_dir}")

    _deploy_runtime_contents("${_extract_dir}/bin/x64-win" "${_runtime_dst_dir}" "${_pdb_dst_dir}" "${_import_dst_dir}")

    file(REMOVE_RECURSE "${_extract_dir}")
endfunction()
