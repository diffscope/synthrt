# T-P1-02: switched from M-3 (native add_library) to M-1 (BuildAPI macro).
# BI-21b: replaced the hand-written set(DSSESSION_*) + include(BuildAPI.cmake)
# boilerplate with synthrt_declare_target, which sets DSSESSION_MACRO_PREFIX=
# synthrt internally so the generated macro family is synthrt_add_library /
# synthrt_install (Q12 decision A), matching lib/Core / lib/G2P / ds-bank /
# ds-infer. The parent project(dssession ...) in domains/ds-session/CMakeLists.txt
# provides PROJECT_NAME=dssession so BuildAPI derives _CUR_PREFIX=dssession and
# reads DSSESSION_MACRO_PREFIX=synthrt, producing the synthrt_add_library macro.
synthrt_declare_target(dssession "srt-ds-session" ${PROJECT_VERSION} "synthrt" 2020 "../../../include")

file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp")

# WP3: VoicebankSession public header includes <synthrt/G2P/Base/LangCommon.h>
# (for srt::g2p::G2pInput/G2pRes used in convertG2p signature), so srt::g2p
# must be PUBLIC. srt::g2p transitively links srt::s2p (LanguageResource used
# by convertS2p implementation).
# WP4: VoicebankSession::createModelSet uses SingerStageResolver/ModelSet from
# ds-infer (included via <diffsinger/Infer/*.h>). Must be PUBLIC because
# ModelSetHandle.h (a public header) includes <diffsinger/Infer/ModelSet.h>.
#
# NO_INSTALL here selects the "sub-library aggregated export set" pattern
# (documented in cmake/BuildAPI.cmake at the NO_INSTALL handling logic):
# srt-ds-session is NOT installed into its own dssessionTargets export set.
# Instead it is aggregated into the parent synthrtTargets export set via the
# hand-written install(TARGETS ... EXPORT synthrtTargets) below, so that
# find_package(synthrt) exposes srt::session as a component of the unified
# synthrt package rather than a separate dssession package. This matches the
# aggregate COMPONENTS wiring in cmake/synthrtAggregate.cmake.
synthrt_add_library(srt-ds-session SHARED NO_INSTALL PREFIX dssession
    SOURCES ${_src}
    LINKS srt-ds::bank srt::g2p srt-ds::infer
)
target_compile_definitions(srt-ds-session PRIVATE DSSESSION_LIBRARY)

if(NOT TARGET srt-ds::session)
    add_library(srt-ds::session ALIAS srt-ds-session)
endif()

# Install into the synthrtTargets aggregate export set. This hand-written
# install(TARGETS ... EXPORT synthrtTargets) is the legitimate workaround that
# completes the "sub-library aggregated export set" mode selected by NO_INSTALL
# above: BuildAPI's internal install block is skipped, so the target must be
# attached to the parent export set here. srt-ds::session is a component of
# synthrt-synthrt. INSTALL_INTERFACE include path is added explicitly here
# (CM-04 fix): NO_INSTALL skips BuildAPI's internal install block that would
# otherwise emit it.
#
# EXPORT_NAME=session + top-level install(EXPORT synthrtTargets NAMESPACE srt::)
# yields the imported target name "srt::session" in synthrtTargets.cmake. The
# build-tree ALIAS "srt-ds::session" (declared above) is consumed by aggregate
# COMPONENTS; CMake rewrites it to the exported name "srt::session" when the
# synthrt-synthrt / srt-diffsinger aggregates are exported.
if(SYNTHRT_INSTALL)
    set_target_properties(srt-ds-session PROPERTIES EXPORT_NAME session)
    install(TARGETS srt-ds-session EXPORT synthrtTargets)
    target_include_directories(srt-ds-session INTERFACE
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    )
endif()
