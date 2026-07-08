#include <dsinfer/dsinfer_global.h>

// Provide a single exported symbol so that the linker generates dsinfer.dll
// and dsinfer.lib even though all real implementations now live in dedicated
// translation units (InferenceService.cpp, ModelRegistry.cpp, SpeakerMapper.cpp)
// and ultimately in srt-core / srt-driver.
// Downstream plugins link against dsinfer.lib for the API headers; the
// actual runtime symbols resolve through srt-core.dll / srt-driver.dll.
//
// This function returns the dsinfer ABI version. It is intentionally kept
// minimal to avoid pulling in any dependencies that belong to srt-core.
extern "C" DSINFER_EXPORT int dsinfer_abi_version() {
    return 1;
}
