#ifndef SYNTHRT_PLUGINFACTORY_P_H
#define SYNTHRT_PLUGINFACTORY_P_H

#include <map>
#include <unordered_set>
#include <shared_mutex>

#include <stdcorelib/3rdparty/llvm/smallvector.h>
#include <stdcorelib/support/sharedlibrary.h>

#include <synthrt/Plugin/PluginFactory.h>

namespace srt {

    class SYNTHRT_EXPORT PluginFactory::Impl {
    public:
        explicit Impl(PluginFactory *decl);
        virtual ~Impl();

        using Decl = PluginFactory;
        PluginFactory *_decl;

    public:
        void scanPlugins(const char *iid) const;

        std::map<std::string, llvm::SmallVector<std::filesystem::path>, std::less<>> pluginPaths;
        std::unordered_set<Plugin *> runtimePlugins;
        mutable std::map<std::filesystem::path::string_type, stdc::SharedLibrary *, std::less<>>
            libraryInstances;
        mutable std::unordered_set<std::string> pluginsDirty;
        mutable std::map<std::string, std::map<std::string, Plugin *>, std::less<>> allPlugins;
        mutable std::shared_mutex plugins_mtx;
    };

}

#endif // SYNTHRT_PLUGINFACTORY_P_H