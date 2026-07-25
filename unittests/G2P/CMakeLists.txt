file(GLOB _g2p_tests CONFIGURE_DEPENDS "tst_*.cpp")

# T-04 (BF-53): chain process error propagation tests need access to the
# chain plugin internals (G2pPipeline / G2pContext / G2pStep). The chain
# plugin is built as a SHARED library with private headers
# (INCLUDE_PRIVATE *) and non-exported symbols, so the tests compile the
# needed internal sources directly into the test target. This is test-only
# wiring; no product code is modified.
#
# The chain plugin's find_package(re2) creates the re2::re2 IMPORTED target
# in plugins/G2P/chain/ scope, which does NOT propagate to this sibling test
# directory. So we must re-discover re2 HERE to make re2::re2 visible. Use
# QUIET to avoid a noisy message when re2 is unavailable; the
# _chain_internals_available flag gates both the T-04 test source inclusion
# and the chain-internal wiring below. When re2 is unavailable, the T-04
# test file is excluded from the glob so the build stays green.
set(_chain_internals_available FALSE)
if(TARGET ChainG2p)
    find_package(re2 CONFIG QUIET)
    if(TARGET re2::re2)
        set(_chain_internals_available TRUE)
    endif()
endif()

if(NOT _chain_internals_available)
    list(REMOVE_ITEM _g2p_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tst_chain_error_propagation_extreme.cpp")
endif()

if(_g2p_tests)
    synthrt_add_unittest(synthrt-unittest-g2p "${_g2p_tests}")
    target_link_libraries(synthrt-unittest-g2p PRIVATE srt::g2p)

    if(_chain_internals_available)
        set(_chain_internal_dir "${CMAKE_SOURCE_DIR}/plugins/G2P/chain/internal")
        target_sources(synthrt-unittest-g2p PRIVATE
            "${_chain_internal_dir}/Core/G2pPipeline.cpp"
            "${_chain_internal_dir}/Core/G2pStep.cpp"
            "${_chain_internal_dir}/Steps/TagAndValidateStep.cpp"
            "${_chain_internal_dir}/Steps/DictStep.cpp"
            "${_chain_internal_dir}/Steps/ModelStep.cpp"
            "${_chain_internal_dir}/Steps/FormatStep.cpp"
            "${_chain_internal_dir}/Steps/FallbackStep.cpp"
        )
        # Chain internal headers reference each other via "internal/..." and
        # "../Core/..." paths rooted at the chain plugin directory.
        # The test file uses #include "internal/Core/G2pPipeline.h" (rooted at
        # chain/), while the chain sources use #include "Steps/..." and
        # "Core/..." (rooted at chain/internal/), so both dirs must be on the
        # include path.
        target_include_directories(synthrt-unittest-g2p PRIVATE
            "${CMAKE_SOURCE_DIR}/plugins/G2P/chain"
            "${CMAKE_SOURCE_DIR}/plugins/G2P/chain/internal"
        )
        target_link_libraries(synthrt-unittest-g2p PRIVATE re2::re2)
        if(TARGET srt-g2p-inferutil)
            target_link_libraries(synthrt-unittest-g2p PRIVATE srt-g2p-inferutil)
        endif()
        # The chain sources do not use Catch2 and were not written against the
        # test target's precompiled header; skip PCH for them to avoid any
        # header-ordering surprises.
        set_source_files_properties(
            "${_chain_internal_dir}/Core/G2pPipeline.cpp"
            "${_chain_internal_dir}/Core/G2pStep.cpp"
            "${_chain_internal_dir}/Steps/TagAndValidateStep.cpp"
            "${_chain_internal_dir}/Steps/DictStep.cpp"
            "${_chain_internal_dir}/Steps/ModelStep.cpp"
            "${_chain_internal_dir}/Steps/FormatStep.cpp"
            "${_chain_internal_dir}/Steps/FallbackStep.cpp"
            PROPERTIES SKIP_PRECOMPILE_HEADERS ON
        )
    endif()
endif()
