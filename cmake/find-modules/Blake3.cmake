# BLAKE3 for the libraries built on synthrt, without the C++ standard its package asks for.
#
# The exported BLAKE3::blake3 carries INTERFACE_COMPILE_FEATURES c_std_99 and cxx_std_20, so every
# target that links it, privately as well, compiles as C++20. blake3.h is a C header, and the
# libraries call only its C interface, so the C++ requirement is spurious: it would be the only
# reason a target of this stack leaves C++17. CMake takes the highest standard over a target's own
# features and the interface features of everything it links, so it cannot be outvoted downstream,
# only removed from the imported target.

include_guard(GLOBAL)

# synthrt_find_blake3()
#
# Finds the blake3 package and leaves BLAKE3::blake3 with the C feature it really needs. An
# imported target belongs to the directory that found it, so a directory that links BLAKE3::blake3
# calls this function itself, in place of find_package(blake3).
macro(synthrt_find_blake3)
    find_package(blake3 CONFIG REQUIRED)
    set_property(TARGET BLAKE3::blake3 PROPERTY INTERFACE_COMPILE_FEATURES c_std_99)
endmacro()
