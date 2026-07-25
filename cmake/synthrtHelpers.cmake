include_guard(DIRECTORY)

# 注: 使用 macro 而非 function，确保 BuildAPI.cmake 的 _CUR_* 变量留在调用方作用域
# synthrt_declare_target: 统一声明宏，替代每库重复的 6 行 set(<prefix>_*) boilerplate
# 默认设置 _MACRO_PREFIX=synthrt，使 BuildAPI.cmake 生成的宏名为 synthrt_add_library 等
#
# 参数：
#   prefix       - 库前缀（如 srt_core, srt_g2p, dsbank, dsinfer）
#   name         - 安装名
#   version      - 版本号
#   author       - 作者
#   start_year   - 起始年份
#   include_dir  - include 目录
#
# 用法：
#   synthrt_declare_target(srt_core "synthrt-core" ${SYNTHRT_VERSION} "synthrt" 2020 "include")
#   synthrt_add_library(${PROJECT_NAME} STATIC ...)

macro(synthrt_declare_target prefix name version author start_year include_dir)
    # BuildAPI derives _CUR_PREFIX from ${PROJECT_NAME}_VAR_PREFIX. PROJECT_NAME
    # may contain hyphens (e.g. "srt-core") while prefix uses underscores (e.g.
    # "srt_core"); set the VAR_PREFIX under the PROJECT_NAME key so BuildAPI
    # resolves _CUR_PREFIX = prefix (not the hyphenated PROJECT_NAME).
    set(${PROJECT_NAME}_VAR_PREFIX ${prefix})
    string(TOUPPER ${prefix} _prefix_upper)
    # BuildAPI reads ${_CUR_PREFIX_UPPER}_* (uppercase prefix). Set all vars
    # under the uppercase key so BuildAPI picks them up correctly. The legacy
    # hand-written form set exactly these uppercase variables (e.g. SRT_CORE_*),
    # so this reproduces the original behaviour 1:1.
    set(${_prefix_upper}_INSTALL ${SYNTHRT_INSTALL})
    set(${_prefix_upper}_VERSION ${version})
    set(${_prefix_upper}_INSTALL_NAME ${name})
    set(${_prefix_upper}_AUTHOR "${author}")
    set(${_prefix_upper}_START_YEAR ${start_year})
    set(${_prefix_upper}_INCLUDE_DIR ${include_dir})
    # 关键：统一宏前缀为 synthrt，生成的宏名为 synthrt_add_library 等
    set(${_prefix_upper}_MACRO_PREFIX synthrt)
    include("${SYNTHRT_SOURCE_DIR}/cmake/BuildAPI.cmake")
endmacro()
