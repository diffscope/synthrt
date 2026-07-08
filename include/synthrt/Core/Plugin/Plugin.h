#ifndef SRT_CORE_PLUGIN_PLUGIN_H
#define SRT_CORE_PLUGIN_PLUGIN_H

#include <filesystem>

#include <synthrt/Core/srt_core_global.h>

namespace srt::core {

    /// Plugin - Base class for all plugins.
    class SRT_CORE_EXPORT Plugin {
    public:
        virtual ~Plugin();

    public:
        /// Returns the interface identifier of the plugin.
        virtual const char *iid() const = 0;

        /// Returns the key of the plugin.
        virtual const char *key() const = 0;

    public:
        std::filesystem::path path() const;
    };

    class StaticPlugin {
    public:
        using PluginInstanceFunction = Plugin *(*) ();

        constexpr StaticPlugin(PluginInstanceFunction i) : instance(i) {
        }

        PluginInstanceFunction instance = nullptr;

    public:
        SRT_CORE_EXPORT static void registerStaticPlugin(const char *pluginSet, StaticPlugin plugin);
    };

}

/// SRT_EXPORT_PLUGIN - Universal plugin registration macro.
///
/// This is the single, unified registration entry point for **all** plugin
/// types in the v4 architecture (G2P task, driver, inference interpreter,
/// singer provider, ...). Each plugin DLL exports exactly one
/// \c srt_plugin_instance() C symbol, which the PluginFactory resolves by
/// (iid, key) and down-casts to the concrete plugin contract
/// (e.g. \c srt::language::IG2pTask).
///
/// Usage:
///   class MyPlugin : public srt::core::Plugin, public srt::language::IG2pTask {
///       ... implement iid(), key(), g2pId(), initialize(), convert() ...
///   };
///   SRT_EXPORT_PLUGIN(MyPlugin)
///
/// The legacy namespace-specific wrapper macros (SRT_G2P_DEFINE_TASK_PLUGIN,
/// SRT_G2P_DEFINE_DRIVER_PLUGIN) are thin sugar over this macro and will be
/// removed once the legacy plugins/G2P tree is deleted in a later phase.
///
/// The future runtime-scoped \c srt_plugin_register C entry point
/// (see 04-plugin-abi-contract.md section 3.2) is a Phase 10 ABI rebuild task
/// and is intentionally NOT introduced here; until then SRT_EXPORT_PLUGIN
/// remains the single registration mechanism.
#define SRT_EXPORT_PLUGIN(PLUGIN_NAME)                                                              \
    extern "C" STDCORELIB_DECL_EXPORT srt::core::Plugin *srt_plugin_instance() {                    \
        static PLUGIN_NAME _instance;                                                               \
        return &_instance;                                                                          \
    }

#define SRT_EXPORT_STATIC_PLUGIN(PLUGIN_NAME, PLUGIN_SET)                                           \
    struct initializer {                                                                            \
        initializer() {                                                                             \
            srt::core::StaticPlugin::registerStaticPlugin(                                          \
                PLUGIN_SET,                                                                         \
                srt::core::StaticPlugin([]() -> srt::core::Plugin * {                              \
                    static PLUGIN_NAME _instance;                                                   \
                    return &_instance;                                                              \
                }));                                                                                \
        }                                                                                           \
        ~initializer() {                                                                            \
        }                                                                                           \
    } dummy;

#endif // SRT_CORE_PLUGIN_PLUGIN_H
