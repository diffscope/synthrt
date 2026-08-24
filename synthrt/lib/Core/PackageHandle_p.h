#ifndef SYNTHRT_PACKAGEHANDLE_P_H
#define SYNTHRT_PACKAGEHANDLE_P_H

#include "PackageHandle.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <stdcorelib/adt/vlarray.h>

namespace srt {

    class ContribExecInstance;

    class PackageData : public std::enable_shared_from_this<PackageData> {
    public:
        explicit PackageData(SynthUnit *synthUnit) : synthUnit(synthUnit) {
        }
        ~PackageData();

        SynthUnit *synthUnit;
        std::string id;
        stdc::VersionNumber version;
        stdc::VersionNumber compatVersion;
        int runtimeLevel = 0;
        bool loaded = false;
        JsonObject manifestDeclaration;
        DisplayText name;
        DisplayText description;
        DisplayText vendor;
        DisplayText readme;
        DisplayText copyright;
        std::string url;
        std::filesystem::path path;
        stdc::vlarray<PackageDependency> dependencies;
        std::map<std::string, std::shared_ptr<PackageData>, std::less<>> dependencyBindings;
        stdc::vlarray<std::unique_ptr<ContribSpec>> ownedContributions;
        std::map<std::string, std::vector<ContribSpec *>, std::less<>> contributions;
        std::map<std::string, std::map<std::string, ContribSpec *, std::less<>>, std::less<>>
            contributionIndex;
        stdc::vlarray<ContribExecInstance *> execInstances;
    };

}

#endif // SYNTHRT_PACKAGEHANDLE_P_H
