# ffmpeg-builds —— 直接消费第三方预编译的 FFmpeg 动态库，不做任何源码编译。
#
#   Windows / Linux : BtbN/FFmpeg-Builds     （自带 MSVC 用的 .lib 导入库，依赖全部静态内联）
#   macOS           : PyAV-Org/pyav-ffmpeg   （BtbN 不出 macOS；该仓库随 FFmpeg 版本按月更新）
#
# 下载清单（文件名 + SHA512）在 assets.cmake 里，由同目录的 update-assets.ps1 生成。

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    message(FATAL_ERROR "ffmpeg-builds 只提供动态库，请使用 dynamic linkage 的三元组（如 x64-windows）。")
endif()

# 预编译产物由 mingw-w64 / clang 交叉编译，dumpbin 的 CRT 与链接方式检查对它不适用
set(VCPKG_POLICY_SKIP_DUMPBIN_CHECKS enabled)

include("${CMAKE_CURRENT_LIST_DIR}/assets.cmake")

# ---------------------------------------------------------------- 选择产物

if("master" IN_LIST FEATURES AND "ffmpeg7" IN_LIST FEATURES)
    message(FATAL_ERROR "ffmpeg-builds: master 与 ffmpeg7 只能二选一。")
endif()

if("master" IN_LIST FEATURES)
    set(FF_LINE "master")       # FFmpeg master 快照
elseif("ffmpeg7" IN_LIST FEATURES)
    set(FF_LINE "v7_1")         # 7.1 release 分支
else()
    set(FF_LINE "v8_1")         # 默认：8.1 release 分支
endif()

if("gpl" IN_LIST FEATURES)
    set(FF_VARIANT "gpl")
else()
    set(FF_VARIANT "lgpl")
endif()

if(VCPKG_TARGET_IS_WINDOWS)
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        set(FF_TARGET "win64")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(FF_TARGET "winarm64")
    endif()
elseif(VCPKG_TARGET_IS_OSX)
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(FF_TARGET "macos-arm64")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        set(FF_TARGET "macos-x86_64")
    endif()
    # pyav-ffmpeg 的 macOS 构建带 libx264/libx265，只有 GPL 一种口味
    if(NOT FF_VARIANT STREQUAL "gpl")
        message(STATUS "ffmpeg-builds: macOS 上游只提供 GPL 构建（含 x264/x265），已忽略 lgpl 选择。")
        set(FF_VARIANT "gpl")
    endif()
    if(NOT FF_LINE STREQUAL "${FFMPEG_BUILDS_PYAV_LINE}")
        message(FATAL_ERROR
            "ffmpeg-builds: macOS 只有 ${FFMPEG_BUILDS_PYAV_LINE} 这一条线（来自 pyav-ffmpeg），"
            "请去掉 master / ffmpeg7 特性。")
    endif()
elseif(VCPKG_TARGET_IS_LINUX)
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        set(FF_TARGET "linux64")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(FF_TARGET "linuxarm64")
    endif()
endif()

if(NOT DEFINED FF_TARGET)
    message(FATAL_ERROR "ffmpeg-builds: 上游没有 ${VCPKG_CMAKE_SYSTEM_NAME}/${VCPKG_TARGET_ARCHITECTURE} 的预编译产物。")
endif()

set(FF_KEY "FFMPEG_BUILDS_${FF_LINE}_${FF_TARGET}_${FF_VARIANT}")
if(NOT DEFINED ${FF_KEY}_FILE)
    message(FATAL_ERROR
        "ffmpeg-builds: assets.cmake 里没有 ${FF_LINE}/${FF_TARGET}/${FF_VARIANT} 的记录。\n"
        "请运行 update-assets.ps1 重新生成清单。")
endif()

set(FF_FILE "${${FF_KEY}_FILE}")
set(FF_SHA512 "${${FF_KEY}_SHA512}")
if(DEFINED ${FF_KEY}_LOCAL)
    # pyav-ffmpeg 的资产名不带版本号，落到 downloads 目录时改名以免不同 tag 撞名
    set(FF_LOCAL "${${FF_KEY}_LOCAL}")
else()
    set(FF_LOCAL "${FF_FILE}")
endif()

if(VCPKG_TARGET_IS_OSX)
    set(FF_URL "https://github.com/PyAV-Org/pyav-ffmpeg/releases/download/${FFMPEG_BUILDS_PYAV_TAG}/${FF_FILE}")
else()
    set(FF_URL "https://github.com/BtbN/FFmpeg-Builds/releases/download/${FFMPEG_BUILDS_BTBN_TAG}/${FF_FILE}")
endif()

message(STATUS "ffmpeg-builds: ${FF_LINE}/${FF_TARGET}/${FF_VARIANT} -> ${FF_FILE}")

vcpkg_download_distfile(ARCHIVE
    URLS "${FF_URL}"
    FILENAME "${FF_LOCAL}"
    SHA512 "${FF_SHA512}"
)

if(VCPKG_TARGET_IS_OSX)
    # pyav 的包直接以 include/ lib/ 为根，没有顶层目录
    vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}" NO_REMOVE_ONE_LEVEL)
else()
    vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")
endif()

# ---------------------------------------------------------------- 组件清单

set(FF_COMPONENTS "avutil") # avutil 是所有库的基础，恒装
foreach(_comp IN ITEMS swresample swscale avcodec avformat avfilter avdevice)
    if("${_comp}" IN_LIST FEATURES)
        list(APPEND FF_COMPONENTS "${_comp}")
    endif()
endforeach()

# 归档文件名里带着真实版本：release 线是 ffmpeg-n8.1.2-…，master 线是 ffmpeg-N-125365-…
if(DEFINED FFMPEG_BUILDS_${FF_LINE}_VERSION)
    set(FF_VERSION "${FFMPEG_BUILDS_${FF_LINE}_VERSION}")
elseif(FF_FILE MATCHES "^ffmpeg-(N-[0-9]+-g[0-9a-f]+)-")
    set(FF_VERSION "${CMAKE_MATCH_1}")
else()
    set(FF_VERSION "unknown")
endif()

# 组件之间的链接依赖（vcpkg.json 的 feature 依赖保证了闭包，这里只描述顺序关系）
set(FF_DEPS_avutil "")
set(FF_DEPS_swresample "avutil")
set(FF_DEPS_swscale "avutil")
set(FF_DEPS_avcodec "swresample;avutil")
set(FF_DEPS_avformat "avcodec;avutil")
set(FF_DEPS_avfilter "avformat;avcodec;swscale;swresample;avutil")
set(FF_DEPS_avdevice "avfilter;avformat;avcodec;avutil")

# ---------------------------------------------------------------- 安装头文件

foreach(_comp IN LISTS FF_COMPONENTS)
    set(_incdir "lib${_comp}")
    if(NOT EXISTS "${SOURCE_PATH}/include/${_incdir}")
        message(FATAL_ERROR "ffmpeg-builds: 归档里缺少 include/${_incdir}")
    endif()
    file(COPY "${SOURCE_PATH}/include/${_incdir}" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
endforeach()

# ---------------------------------------------------------------- 安装二进制

file(MAKE_DIRECTORY
    "${CURRENT_PACKAGES_DIR}/lib"
    "${CURRENT_PACKAGES_DIR}/debug/lib"
)

if(VCPKG_TARGET_IS_WINDOWS)
    file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin")
    foreach(_comp IN LISTS FF_COMPONENTS)
        # 导入库：avcodec.lib / avutil.lib …
        if(NOT EXISTS "${SOURCE_PATH}/lib/${_comp}.lib")
            message(FATAL_ERROR "ffmpeg-builds: 归档里缺少 lib/${_comp}.lib")
        endif()
        file(COPY "${SOURCE_PATH}/lib/${_comp}.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
        file(COPY "${SOURCE_PATH}/lib/${_comp}.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")

        # 运行库：avcodec-62.dll …（版本号随 FFmpeg 大版本变，用通配符取）
        file(GLOB _dll "${SOURCE_PATH}/bin/${_comp}-*.dll")
        list(LENGTH _dll _dll_count)
        if(NOT _dll_count EQUAL 1)
            message(FATAL_ERROR "ffmpeg-builds: bin/${_comp}-*.dll 匹配到 ${_dll_count} 个文件，期望 1 个")
        endif()
        file(COPY "${_dll}" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
        file(COPY "${_dll}" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")

        get_filename_component(_dll_name "${_dll}" NAME)
        set(FF_RUNTIME_${_comp} "${_dll_name}")
    endforeach()
else()
    if(VCPKG_TARGET_IS_OSX)
        set(_lib_suffix "dylib")
    else()
        set(_lib_suffix "so")
    endif()
    foreach(_comp IN LISTS FF_COMPONENTS)
        # file(COPY) 会把符号链接照抄成符号链接，因此 libavcodec.so / .so.62 / .so.62.28.102 不会被展开成三份实体
        if(VCPKG_TARGET_IS_OSX)
            file(GLOB _libs "${SOURCE_PATH}/lib/lib${_comp}.*dylib")
        else()
            file(GLOB _libs "${SOURCE_PATH}/lib/lib${_comp}.so*")
        endif()
        if(NOT _libs)
            message(FATAL_ERROR "ffmpeg-builds: 归档里缺少 lib/lib${_comp}.*")
        endif()
        file(COPY ${_libs} DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
        file(COPY ${_libs} DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")

        # 记录带 SONAME 的实际文件名，供生成 CMake 配置时使用
        set(FF_RUNTIME_${_comp} "lib${_comp}.${_lib_suffix}")
    endforeach()
endif()

# ---------------------------------------------------------------- macOS：私有依赖 + install_name 修正

if(VCPKG_TARGET_IS_OSX)
    # pyav 的 ffmpeg dylib 会链接同包里的 x264/x265/opus/mp3lame/webp 等。
    # 这些名字和 vcpkg 自己的 opus、libpng、libwebp 端口会撞车，所以放进私有子目录，
    # 并把 ffmpeg dylib 对它们的引用改写成 @loader_path/ffmpeg-builds/...，做到自包含。
    set(FF_PRIVATE_DIR "${CURRENT_PACKAGES_DIR}/lib/ffmpeg-builds")
    file(MAKE_DIRECTORY "${FF_PRIVATE_DIR}" "${CURRENT_PACKAGES_DIR}/debug/lib/ffmpeg-builds")

    file(GLOB _all_dylibs "${SOURCE_PATH}/lib/*.dylib")
    set(FF_PRIVATE_LIBS "")
    foreach(_dylib IN LISTS _all_dylibs)
        get_filename_component(_name "${_dylib}" NAME)
        if(NOT _name MATCHES "^lib(avutil|avcodec|avformat|avfilter|avdevice|swscale|swresample)\\.")
            list(APPEND FF_PRIVATE_LIBS "${_dylib}")
        endif()
    endforeach()
    if(FF_PRIVATE_LIBS)
        file(COPY ${FF_PRIVATE_LIBS} DESTINATION "${FF_PRIVATE_DIR}")
        file(COPY ${FF_PRIVATE_LIBS} DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib/ffmpeg-builds")
    endif()

    # 上游 dylib 的 install_name 是构建机绝对路径（/tmp/vendor/lib/...），必须改写后才能用
    find_program(FF_OTOOL otool REQUIRED)
    find_program(FF_INSTALL_NAME_TOOL install_name_tool REQUIRED)

    function(ff_fix_dylib dylib_path private_prefix)
        get_filename_component(_self "${dylib_path}" NAME)
        # 只对实体文件动手，符号链接跳过
        if(IS_SYMLINK "${dylib_path}")
            return()
        endif()
        execute_process(
            COMMAND "${FF_INSTALL_NAME_TOOL}" -id "@rpath/${_self}" "${dylib_path}"
            RESULT_VARIABLE _res
        )
        if(NOT _res EQUAL 0)
            message(FATAL_ERROR "install_name_tool -id 失败: ${dylib_path}")
        endif()

        execute_process(
            COMMAND "${FF_OTOOL}" -L "${dylib_path}"
            OUTPUT_VARIABLE _otool_out
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        string(REPLACE "\n" ";" _otool_lines "${_otool_out}")
        foreach(_line IN LISTS _otool_lines)
            string(REGEX MATCH "^\t([^ ]+) " _m "${_line}")
            if(NOT _m)
                continue()
            endif()
            set(_dep "${CMAKE_MATCH_1}")
            if(_dep MATCHES "^(/usr/lib|/System)")
                continue()
            endif()
            if(_dep MATCHES "^@")
                continue()
            endif()
            get_filename_component(_dep_name "${_dep}" NAME)
            if(_dep_name MATCHES "^lib(avutil|avcodec|avformat|avfilter|avdevice|swscale|swresample)\\.")
                set(_new "@rpath/${_dep_name}")
            else()
                set(_new "${private_prefix}${_dep_name}")
            endif()
            execute_process(
                COMMAND "${FF_INSTALL_NAME_TOOL}" -change "${_dep}" "${_new}" "${dylib_path}"
                RESULT_VARIABLE _res
            )
            if(NOT _res EQUAL 0)
                message(FATAL_ERROR "install_name_tool -change 失败: ${dylib_path} (${_dep})")
            endif()
        endforeach()
    endfunction()

    foreach(_dir IN ITEMS "${CURRENT_PACKAGES_DIR}/lib" "${CURRENT_PACKAGES_DIR}/debug/lib")
        file(GLOB _installed "${_dir}/*.dylib")
        foreach(_f IN LISTS _installed)
            # ffmpeg 本体：私有依赖在 lib/ffmpeg-builds/ 下
            ff_fix_dylib("${_f}" "@loader_path/ffmpeg-builds/")
        endforeach()
        file(GLOB _installed_private "${_dir}/ffmpeg-builds/*.dylib")
        foreach(_f IN LISTS _installed_private)
            # 私有依赖之间在同一层目录
            ff_fix_dylib("${_f}" "@loader_path/")
        endforeach()
    endforeach()
endif()

# ---------------------------------------------------------------- 可执行文件（可选）

if("tools" IN_LIST FEATURES)
    set(FF_TOOL_NAMES ffmpeg ffprobe ffplay)
    set(FF_TOOLS "")
    foreach(_tool IN LISTS FF_TOOL_NAMES)
        if(VCPKG_TARGET_IS_WINDOWS)
            set(_path "${SOURCE_PATH}/bin/${_tool}.exe")
        else()
            set(_path "${SOURCE_PATH}/bin/${_tool}")
        endif()
        if(EXISTS "${_path}")
            list(APPEND FF_TOOLS "${_path}")
        endif()
    endforeach()
    if(FF_TOOLS)
        file(COPY ${FF_TOOLS} DESTINATION "${CURRENT_PACKAGES_DIR}/tools/${PORT}")
        if(VCPKG_TARGET_IS_WINDOWS)
            # 可执行文件与 DLL 同目录才能直接跑
            file(GLOB _runtime "${CURRENT_PACKAGES_DIR}/bin/*.dll")
            file(COPY ${_runtime} DESTINATION "${CURRENT_PACKAGES_DIR}/tools/${PORT}")
        endif()
    endif()
endif()

# ---------------------------------------------------------------- 生成 CMake 配置

set(FF_CONFIG_DIR "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(MAKE_DIRECTORY "${FF_CONFIG_DIR}")

set(FF_TARGET_BLOCKS "")
foreach(_comp IN LISTS FF_COMPONENTS)
    set(_deps "")
    foreach(_dep IN LISTS FF_DEPS_${_comp})
        if(_dep IN_LIST FF_COMPONENTS)
            list(APPEND _deps "FFmpeg::${_dep}")
        endif()
    endforeach()

    if(VCPKG_TARGET_IS_WINDOWS)
        set(_props
"    IMPORTED_IMPLIB_RELEASE \"\${_ffmpeg_builds_prefix}/lib/${_comp}.lib\"
    IMPORTED_LOCATION_RELEASE \"\${_ffmpeg_builds_prefix}/bin/${FF_RUNTIME_${_comp}}\"
    IMPORTED_IMPLIB_DEBUG \"\${_ffmpeg_builds_prefix}/debug/lib/${_comp}.lib\"
    IMPORTED_LOCATION_DEBUG \"\${_ffmpeg_builds_prefix}/debug/bin/${FF_RUNTIME_${_comp}}\"")
    else()
        set(_props
"    IMPORTED_LOCATION_RELEASE \"\${_ffmpeg_builds_prefix}/lib/${FF_RUNTIME_${_comp}}\"
    IMPORTED_LOCATION_DEBUG \"\${_ffmpeg_builds_prefix}/debug/lib/${FF_RUNTIME_${_comp}}\"")
    endif()

    string(APPEND FF_TARGET_BLOCKS
"if(NOT TARGET FFmpeg::${_comp})
  add_library(FFmpeg::${_comp} SHARED IMPORTED)
  set_target_properties(FFmpeg::${_comp} PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES \"\${_ffmpeg_builds_prefix}/include\"
    IMPORTED_CONFIGURATIONS \"RELEASE;DEBUG\"
${_props}
    INTERFACE_LINK_LIBRARIES \"${_deps}\")
endif()
list(APPEND FFMPEG_LIBRARIES FFmpeg::${_comp})

")
endforeach()

set(FF_COMPONENTS_STR "${FF_COMPONENTS}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/ffmpeg-builds-config.cmake.in"
    "${FF_CONFIG_DIR}/ffmpeg-buildsConfig.cmake"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/ffmpeg-builds-config-version.cmake.in"
    "${FF_CONFIG_DIR}/ffmpeg-buildsConfigVersion.cmake"
    @ONLY
)
configure_file("${CMAKE_CURRENT_LIST_DIR}/usage" "${FF_CONFIG_DIR}/usage" @ONLY)

# ---------------------------------------------------------------- 许可

if(EXISTS "${SOURCE_PATH}/LICENSE.txt")
    vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
else()
    # pyav-ffmpeg 的归档里不带 LICENSE 文本
    file(WRITE "${FF_CONFIG_DIR}/copyright"
"FFmpeg is licensed under the LGPL v2.1 or later; this binary distribution is
configured with --enable-version3 and, for the gpl variant (and for every macOS
build), with GPL-only components such as libx264/libx265, which makes the
resulting binaries GPL v3.

Upstream binaries:
  Windows/Linux: https://github.com/BtbN/FFmpeg-Builds
  macOS:         https://github.com/PyAV-Org/pyav-ffmpeg
Source and full license texts: https://ffmpeg.org/legal.html
")
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
