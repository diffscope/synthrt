// srt-g2p library — out-of-line key functions for exported abstract base
// classes declared in include/synthrt/G2P/Task/.
//
// MSVC does not emit vtable/key-function symbols for exported
// (__declspec(dllexport)) classes whose virtual methods are all inline.
// Plugins linking against srt-g2p.dll then fail with LNK2019. Moving the
// destructor (and iid() for TaskPlugin/DriverPlugin) out-of-line into this
// translation unit forces MSVC to emit the symbols in the DLL.

#include <synthrt/G2P/Task/DictTask.h>
#include <synthrt/G2P/Task/SessionFactory.h>
#include <synthrt/G2P/Task/TaskPlugin.h>
#include <synthrt/G2P/Task/VersionedTaskImplBase.h>

namespace srt::g2p {

    // --- TaskPlugin ---
    TaskPlugin::~TaskPlugin() = default;

    // --- DriverPlugin ---
    DriverPlugin::~DriverPlugin() = default;

    // --- SessionFactory ---
    SessionFactory::~SessionFactory() = default;

    // --- VersionedTaskImplBase ---
    VersionedTaskImplBase::~VersionedTaskImplBase() = default;

    // --- DictInputV1 ---
    DictInputV1::DictInputV1() : TaskInput("DictInputV1") {}

    // --- DictResV1 ---
    DictResV1::DictResV1() : TaskResult("DictResV1") {}

} // namespace srt::g2p
