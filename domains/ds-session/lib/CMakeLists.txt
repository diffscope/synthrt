file(GLOB_RECURSE _src CONFIGURE_DEPENDS "*.cpp")
add_library(srt-ds-session SHARED ${_src})
target_link_libraries(srt-ds-session PUBLIC srt-ds::bank)
# WP3: VoicebankSession public header includes <synthrt/G2P/Base/LangCommon.h>
# (for srt::g2p::G2pInput/G2pRes used in convertG2p signature), so srt::g2p
# must be PUBLIC. srt::g2p transitively links srt::s2p (LanguageResource used
# by convertS2p implementation).
target_link_libraries(srt-ds-session PUBLIC srt::g2p)
# WP4: VoicebankSession::createModelSet uses SingerStageResolver/ModelSet from
# ds-infer (included via <diffsinger/Infer/*.h>). Must be PUBLIC because
# ModelSetHandle.h (a public header) includes <diffsinger/Infer/ModelSet.h>.
target_link_libraries(srt-ds-session PUBLIC srt-ds::infer)
target_include_directories(srt-ds-session PUBLIC $<BUILD_INTERFACE:${SYNTHRT_SOURCE_DIR}/include>)
target_compile_definitions(srt-ds-session PRIVATE DSSESSION_LIBRARY)
add_library(srt-ds::session ALIAS srt-ds-session)

if(SYNTHRT_INSTALL)
    set_target_properties(srt-ds-session PROPERTIES EXPORT_NAME dssession)
    install(TARGETS srt-ds-session EXPORT synthrtTargets)
endif()
