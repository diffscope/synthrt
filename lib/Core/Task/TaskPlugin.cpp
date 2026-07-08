#include <synthrt/Core/Task/TaskPlugin.h>

namespace srt::core {

    TaskPlugin::TaskPlugin() = default;

    TaskPlugin::~TaskPlugin() = default;

    const char *TaskPlugin::iid() const {
        return staticIid();
    }

    const char *TaskPlugin::staticIid() {
        return kTaskPluginIid;
    }

    DriverPlugin::DriverPlugin() = default;

    DriverPlugin::~DriverPlugin() = default;

    const char *DriverPlugin::iid() const {
        return staticIid();
    }

    const char *DriverPlugin::staticIid() {
        return kDriverPluginIid;
    }

} // namespace srt::core
