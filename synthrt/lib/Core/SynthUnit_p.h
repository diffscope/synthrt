#ifndef SYNTHRT_SYNTHUNIT_P_H
#define SYNTHRT_SYNTHUNIT_P_H

#include <map>
#include <memory>
#include <unordered_map>
#include <list>

#include <stdcorelib/adt/vlarray.h>

#include <synthrt/Core/SynthUnit.h>
#include <synthrt/Plugin/PluginFactory_p.h>

namespace srt {

    class ContribHandler;

    class ContribSpec;

    class PackageData;

    class SynthUnit::Impl : public PluginFactory::Impl {
    public:
        explicit Impl(SynthUnit *decl);
        ~Impl();

        using Decl = SynthUnit;

        Expected<PackageData *> open(const std::filesystem::path &path, bool noLoad);
        bool close(PackageData *spec);

    public:
        void closeAllLoadedPackages();
        void refreshPackageIndexes();

        std::map<std::string, std::unique_ptr<ContribHandler>, std::less<>> handlers;

        stdc::vlarray<std::filesystem::path> packagePaths;

        struct LoadedPackageBlock {
            PackageData *spec = nullptr;
            int ref = 0;
            stdc::vlarray<ContribSpec *> contributes;
            stdc::vlarray<PackageData *> linked;
        };
        class LoadedPackageMap {
        public:
            std::list<LoadedPackageBlock> packages;
            std::map<std::filesystem::path::string_type, decltype(packages)::iterator, std::less<>>
                pathIndexes;
            std::map<std::string,
                     std::unordered_map<stdc::VersionNumber, decltype(packages)::iterator>,
                     std::less<>>
                idIndexes;
            std::unordered_map<PackageData *, decltype(packages)::iterator> pointerIndexes;
        };
        LoadedPackageMap loadedPackageMap;
        std::unordered_set<PackageData *> resourcePackages;

        struct PackageBrief {
            std::filesystem::path path;
        };
        bool packagePathsDirty = false;
        std::map<std::string, std::map<stdc::VersionNumber, PackageBrief>, std::less<>>
            cachedPackageIndexesMap;

        // temp
        std::map<std::string, std::unordered_map<stdc::VersionNumber, std::filesystem::path>,
                 std::less<>>
            pendingPackages;

        mutable std::shared_mutex su_mtx;

    };

}

#endif // SYNTHRT_SYNTHUNIT_P_H
