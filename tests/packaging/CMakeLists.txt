set(_packaging_prefix "${CMAKE_BINARY_DIR}/tests/packaging/install-prefix/$<CONFIG>")
set(_packaging_fixture synthrt-packaging-install)
set(_packaging_libdir "${CMAKE_INSTALL_LIBDIR}")
set(_packaging_datadir "${CMAKE_INSTALL_DATADIR}")
if(_packaging_libdir STREQUAL "")
    set(_packaging_libdir lib)
endif()
if(_packaging_datadir STREQUAL "")
    set(_packaging_datadir share)
endif()

add_test(NAME synthrt-test-install-package-setup
    COMMAND "${CMAKE_COMMAND}"
        "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
        "-DINSTALL_PREFIX=${_packaging_prefix}"
        "-DTEST_CONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunInstall.cmake"
)
set_tests_properties(synthrt-test-install-package-setup PROPERTIES
    FIXTURES_SETUP ${_packaging_fixture}
    LABELS packaging
    RESOURCE_LOCK synthrt-packaging-prefix
)

add_test(NAME synthrt-test-install-package
    COMMAND "${CMAKE_COMMAND}"
        "-DINSTALL_PREFIX=${_packaging_prefix}"
        "-DINSTALL_MANIFEST=${CMAKE_BINARY_DIR}/install_manifest.txt"
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
        "-DCMAKE_INSTALL_LIBDIR=${_packaging_libdir}"
        "-DCMAKE_INSTALL_DATADIR=${_packaging_datadir}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertInstallTree.cmake"
)
set_tests_properties(synthrt-test-install-package PROPERTIES
    FIXTURES_REQUIRED ${_packaging_fixture}
    LABELS packaging
    RESOURCE_LOCK synthrt-packaging-prefix
)

add_test(NAME synthrt-test-install-package-scanner-self-test
    COMMAND "${CMAKE_COMMAND}"
        "-DSELF_TEST_ROOT=${CMAKE_BINARY_DIR}/tests/packaging/scanner-self-test"
        "-DG2P_PACKAGES_SOURCE_DIR=${CMAKE_SOURCE_DIR}/resources/G2pPackages"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertInstallTree.cmake"
)
set_tests_properties(synthrt-test-install-package-scanner-self-test PROPERTIES
    LABELS packaging
    RESOURCE_LOCK synthrt-packaging-scanner-self-test
)
