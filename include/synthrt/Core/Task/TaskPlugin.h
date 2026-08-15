#pragma once

#include <synthrt/Core/Plugin/Plugin.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Task/Task.h>

namespace srt::core {

    inline constexpr auto kTaskPluginIid = "srt.core.task";
    inline constexpr auto kDriverPluginIid = "srt.core.driver";

    class SRT_CORE_EXPORT TaskPlugin : public Plugin {
    public:
        TaskPlugin();
        ~TaskPlugin() override;

        const char *iid() const override;
        static const char *staticIid();

        virtual int apiLevel() const = 0;
        virtual Expected<NO<Task>> createTask(const ModuleSpec *spec) = 0;

        STDC_DISABLE_COPY(TaskPlugin)
    };

    class SRT_CORE_EXPORT DriverPlugin : public Plugin {
    public:
        DriverPlugin();
        ~DriverPlugin() override;

        const char *iid() const override;
        static const char *staticIid();

        virtual int apiLevel() const = 0;
        virtual Expected<NO<SessionFactory>> create() = 0;

        STDC_DISABLE_COPY(DriverPlugin)
    };

} // namespace srt::core
