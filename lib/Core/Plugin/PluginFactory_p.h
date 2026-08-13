#pragma once

#include <stdcorelib/support/sharedlibrary.h>
#include <synthrt/Core/Plugin/PluginFactory.h>

#include <map>
#include <memory>
#include <shared_mutex>
#include <unordered_set>

namespace srt::core {

    class SRT_CORE_EXPORT PluginFactory::Impl {
    public:
        explicit Impl(PluginFactory *q);
        virtual ~Impl();

        PluginFactory *m_q;

    public:
        void scanPlugins(const char *iid) const;
        bool preloadSharedLibraries(const std::filesystem::path &sharedDir) const;
        static std::filesystem::path sharedLibraryPath(const std::filesystem::path &categoryDir);

        // Directories per IID (each entry is a parent dir whose subdirectories contain plugin.json)
        std::map<std::string, std::vector<std::filesystem::path>, std::less<>> m_pluginDirs;
        std::map<std::string, std::vector<std::filesystem::path>, std::less<>> m_sharedDirs;
        std::unordered_set<Plugin *>                                           m_runtimePlugins;
        mutable std::unordered_set<std::string>                                m_scannedPluginDirs;
        mutable std::unordered_set<std::filesystem::path::string_type>         m_loadedSharedDirs;
        mutable std::map<std::filesystem::path::string_type, std::unique_ptr<stdc::SharedLibrary>, std::less<>>
            m_preloadedLibraries;
        // CODING-04: own the SharedLibrary via unique_ptr (no bare new/delete).
        // Style matches `m_preloadedLibraries` above; the Impl destructor is
        // defaulted and relies on unique_ptr cleanup.
        mutable std::map<std::filesystem::path::string_type, std::unique_ptr<stdc::SharedLibrary>, std::less<>>
                                                                                    m_libraryInstances;
        mutable std::unordered_set<std::string>                                     m_pluginsDirty;
        mutable std::map<std::string, std::map<std::string, Plugin *>, std::less<>> m_allPlugins;
        mutable std::shared_mutex                                                   m_plugins_mtx;
    };

} // namespace srt::core
