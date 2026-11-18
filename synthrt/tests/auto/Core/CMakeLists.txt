add_auto_test(test_ContribInterpreterPlugin.cpp synthrt)
add_auto_test(test_ContribLocator.cpp synthrt)
add_auto_test(test_SynthUnit.cpp synthrt)
add_auto_test(test_ContribContract.cpp synthrt)
add_auto_test(test_ContribSpecPayload.cpp synthrt)
add_auto_test(test_ContribCategory.cpp synthrt)
add_auto_test(test_RuntimeService.cpp synthrt)
add_auto_test(test_PackageHandle.cpp synthrt)
add_auto_test(test_ContribExecutive.cpp synthrt)

if(TARGET test_ContribExecutive)
    add_test(NAME test_PackageExecutiveFatal
        COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:test_ContribExecutive>
        -DVARIANT=package
        -DEXPECTED=still
        -P ${CMAKE_CURRENT_SOURCE_DIR}/ExpectFatal.cmake
    )

    add_test(NAME test_SynthUnitExecutiveFatal
        COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:test_ContribExecutive>
        -DVARIANT=unit
        -DEXPECTED=SynthUnit
        -P ${CMAKE_CURRENT_SOURCE_DIR}/ExpectFatal.cmake
    )
endif()
