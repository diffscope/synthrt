#pragma once

#include <synthrt/Core/Plugin/Plugin.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/G2P/srt_g2p_global.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Task/SessionTask.h>
#include <synthrt/G2P/Task/SessionFactory.h>

namespace srt::g2p {

    /// TaskPlugin - Base class for G2P task plugins.
    ///
    /// Migrated from LangCore::TaskPlugin. Each G2P plugin (MandarinG2p,
    /// CantoneseG2p, LstmG2p, ChainG2p, DsDict) implements createTask() to
    /// produce a Task instance from a ModuleSpec.
    class SRT_G2P_EXPORT TaskPlugin : public srt::core::Plugin {
    public:
        TaskPlugin() = default;
        ~TaskPlugin() override;

        const char *iid() const override { return staticIid(); }

        /// Static IID accessor — allows PluginFactory::plugin<T>(key) to
        /// obtain the IID without invoking the virtual method on a null
        /// pointer (UB).
        static const char *staticIid() { return kTaskPluginIid; }

        /// API level of the plugin (used for compatibility checks).
        virtual int apiLevel() const = 0;

        virtual srt::core::Expected<srt::core::NO<Task>> createTask(const ModuleSpec *spec) = 0;

        STDC_DISABLE_COPY(TaskPlugin)
    };

    /// DriverPlugin - Base class for G2P driver plugins.
    ///
    /// Migrated from LangCore::DriverPlugin. The G2P OnnxDriver plugin
    /// implements create() to produce a SessionFactory instance.
    class SRT_G2P_EXPORT DriverPlugin : public srt::core::Plugin {
    public:
        DriverPlugin() = default;
        ~DriverPlugin() override;

        const char *iid() const override { return staticIid(); }

        static const char *staticIid() { return kDriverPluginIid; }

        virtual int apiLevel() const = 0;

        virtual srt::core::Expected<srt::core::NO<SessionFactory>> create() = 0;

        STDC_DISABLE_COPY(DriverPlugin)
    };

} // namespace srt::g2p
