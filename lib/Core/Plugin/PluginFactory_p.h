#ifndef SRT_CORE_PLUGIN_PLUGINFACTORY_P_H
#define SRT_CORE_PLUGIN_PLUGINFACTORY_P_H

#include <map>
#include <memory>
#include <unordered_set>
#include <shared_mutex>

#include <stdcorelib/3rdparty/llvm/smallvector.h>
#include <stdcorelib/support/sharedlibrary.h>

#include <synthrt/Core/Plugin/PluginFactory.h>

namespace srt::core {

    class SRT_CORE_EXPORT PluginFactory::Impl {
    public:
        explicit Impl(PluginFactory *decl);
        virtual ~Impl();

        using Decl = PluginFactory;
        PluginFactory *_decl;

    public:
        void scanPlugins(const char *iid) const;
        bool preloadSharedLibraries(const std::filesystem::path &sharedDir) const;
        static std::filesystem::path sharedLibraryPath(const std::filesystem::path &categoryDir);

        // Directories per IID (each entry is a parent dir whose subdirectories contain plugin.json)
        std::map<std::string, llvm::SmallVector<std::filesystem::path>, std::less<>> pluginDirs;
        std::map<std::string, llvm::SmallVector<std::filesystem::path>, std::less<>> sharedDirs;
        std::unordered_set<Plugin *> runtimePlugins;
        mutable std::unordered_set<std::string> scannedPluginDirs;
        mutable std::unordered_set<std::filesystem::path::string_type> loadedSharedDirs;
        mutable std::map<std::filesystem::path::string_type,
                         std::unique_ptr<stdc::SharedLibrary>, std::less<>> preloadedLibraries;
        mutable std::map<std::filesystem::path::string_type, stdc::SharedLibrary *, std::less<>>
            libraryInstances;
        mutable std::unordered_set<std::string> pluginsDirty;
        mutable std::map<std::string, std::map<std::string, Plugin *>, std::less<>> allPlugins;
        mutable std::shared_mutex plugins_mtx;
    };

}

#endif // SRT_CORE_PLUGIN_PLUGINFACTORY_P_H
