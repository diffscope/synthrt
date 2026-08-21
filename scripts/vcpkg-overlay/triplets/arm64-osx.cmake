set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

# Debug info comes from a flag rather than from building the ports as RelWithDebInfo. The build
# type is not free to change: vcpkg_cmake_config_fixup only rewrites the tool paths it finds in
# *-release.cmake, and the imported targets ports export are looked up as Release.
set(VCPKG_C_FLAGS_RELEASE "-g")
set(VCPKG_CXX_FLAGS_RELEASE "-g")