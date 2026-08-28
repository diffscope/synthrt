set(VCPKG_POLICY_ALLOW_DLLS_IN_LIB enabled)
set(VCPKG_POLICY_SKIP_LIB_CMAKE_MERGE_CHECK enabled)
set(VCPKG_POLICY_SKIP_MISPLACED_CMAKE_FILES_CHECK enabled)
set(VCPKG_POLICY_ALLOW_EMPTY_FOLDERS enabled)
# Payload-only port: binaries/headers intentionally live under include/ and
# bin/ (see below), not in the conventional INTERFACE include layout.
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
# Release-only payload (downloaded binaries, no debug build follows the
# same ABI); suppress the mismatched debug/release count check.
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
# Import libraries live under import/<flavor>/ (whitelisted, per-flavor
# private), so bin/ DLLs intentionally lack import libs in the conventional
# lib/ location.
set(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        cuda12 WITH_CUDA12
)

# Platform support matrix: upstream payload assets only exist for these
# combinations. Fail fast instead of silently deploying mismatched binaries
# (the Windows NuGet package always ships win-x64 contents, so non-x64
# triplets would otherwise pick up x64 DLLs of the wrong ABI).
if(VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    message(FATAL_ERROR
        "onnxruntime-builds: only Windows x64 is supported (triplet: ${VCPKG_TARGET_TRIPLET})")
endif()
if(VCPKG_TARGET_IS_OSX AND NOT VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    message(FATAL_ERROR
        "onnxruntime-builds: only macOS arm64 is supported (triplet: ${VCPKG_TARGET_TRIPLET})")
endif()

# Downloads deploy directly into per-flavor final directories — no
# merge-then-move passes, so default and CUDA payloads never overwrite or
# sweep each other (whitelist split in _deploy_runtime_contents):
#   include/   ONNX Runtime headers (DirectML/CUDA variants also land here)
#   bin/       default runtime DLL mirror (Windows only; vcpkg runtime convention)
#   <share>/runtime/default/  default payload (DirectML on Windows), pure DLLs
#   <share>/runtime/cuda/     CUDA 12 payload (cuda12 feature only), pure DLLs
#   <share>/pdb/<flavor>/     debug symbols (pdb on Windows, dSYM bundles on
#                             macOS), same flavor structure; consumers copy
#                             them on demand — never auto-deployed
#   <share>/import/<flavor>/  import libraries, per-flavor private
# Upstream junk embedding dead paths (lib/cmake configs, pkgconfig) is NOT
# deployed. Consumers find this package via find_package(onnxruntime-builds
# CONFIG) and read ONNXRUNTIME_BUILDS_INCLUDE_DIR / RUNTIME_DIR /
# CUDA_RUNTIME_DIR.

# Layout constants are defined once here and shared with the Config template
# via configure_file(@ONLY) — change the layout in one place only.
set(_ORT_REL_SHARE_DIR "share/onnxruntime-builds")
set(_ORT_REL_RUNTIME_DEFAULT "${_ORT_REL_SHARE_DIR}/runtime/default")
set(_ORT_REL_RUNTIME_CUDA "${_ORT_REL_SHARE_DIR}/runtime/cuda")
set(_ORT_REL_PDB_DEFAULT "${_ORT_REL_SHARE_DIR}/pdb/default")
set(_ORT_REL_PDB_CUDA "${_ORT_REL_SHARE_DIR}/pdb/cuda")
set(_ORT_REL_IMPORT_DEFAULT "${_ORT_REL_SHARE_DIR}/import/default")
set(_ORT_REL_IMPORT_CUDA "${_ORT_REL_SHARE_DIR}/import/cuda")

include("${CMAKE_CURRENT_LIST_DIR}/setup-onnxruntime/setup.cmake")

set(_ort_default_runtime_dir "${CURRENT_PACKAGES_DIR}/${_ORT_REL_RUNTIME_DEFAULT}")
set(_ort_default_pdb_dir "${CURRENT_PACKAGES_DIR}/${_ORT_REL_PDB_DEFAULT}")
set(_ort_default_import_dir "${CURRENT_PACKAGES_DIR}/${_ORT_REL_IMPORT_DEFAULT}")

if(VCPKG_TARGET_IS_WINDOWS)
    message(STATUS "Downloading DirectML version of ONNX Runtime...")
    download_onnxruntime_from_nuget("${CURRENT_PACKAGES_DIR}" "${_ort_default_runtime_dir}"
        "${_ort_default_pdb_dir}" "${_ort_default_import_dir}")
    download_dml_from_nuget("${CURRENT_PACKAGES_DIR}" "${_ort_default_runtime_dir}"
        "${_ort_default_pdb_dir}" "${_ort_default_import_dir}")

    # Mirror the default runtime DLLs into bin/ (vcpkg runtime convention).
    # bin/ is shared with other packages, so consumers must keep globbing the
    # port-private runtime directory instead. (*.dll only — symbols live in
    # pdb/ and import libraries in import/, neither is mirrored.)
    file(GLOB _ort_default_dlls "${_ort_default_runtime_dir}/*.dll")
    if(_ort_default_dlls)
        file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/bin")
        file(COPY ${_ort_default_dlls} DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
    endif()
else()
    message(STATUS "Downloading CPU version of ONNX Runtime...")
    download_onnxruntime_from_github("cpu" "${CURRENT_PACKAGES_DIR}" "${_ort_default_runtime_dir}"
        "${_ort_default_pdb_dir}" "${_ort_default_import_dir}")
endif()

# Optional GPU (CUDA 12) runtime deployment
if(WITH_CUDA12)
    message(STATUS "Downloading GPU (CUDA 12.x) version of ONNX Runtime...")
    download_onnxruntime_from_github("cuda12" "${CURRENT_PACKAGES_DIR}"
        "${CURRENT_PACKAGES_DIR}/${_ORT_REL_RUNTIME_CUDA}"
        "${CURRENT_PACKAGES_DIR}/${_ORT_REL_PDB_CUDA}"
        "${CURRENT_PACKAGES_DIR}/${_ORT_REL_IMPORT_CUDA}")
endif()

# Provide a CMake package so synthrt (and other consumers) can use
# find_package(onnxruntime-builds) without the port passing any path.
# No edit needed for share directory creation (the per-flavor runtime
# directories were already created by the deploy helpers above).
file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/${_ORT_REL_SHARE_DIR}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/onnxruntime-buildsConfig.cmake.in"
    "${CURRENT_PACKAGES_DIR}/${_ORT_REL_SHARE_DIR}/onnxruntime-buildsConfig.cmake"
    @ONLY
)
# PACKAGE_VERSION comes from _version_ort (setup-onnxruntime/versions.cmake)
# instead of a hardcoded string, so a payload version bump only touches one file.
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/onnxruntime-buildsConfigVersion.cmake.in"
    "${CURRENT_PACKAGES_DIR}/${_ORT_REL_SHARE_DIR}/onnxruntime-buildsConfigVersion.cmake"
    @ONLY
)

vcpkg_install_copyright(FILE_LIST "${CMAKE_CURRENT_LIST_DIR}/LICENSE")
