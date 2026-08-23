#ifndef SYNTHRT_PACKAGEHANDLE_P_H
#define SYNTHRT_PACKAGEHANDLE_P_H

#include "PackageHandle.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace srt {

    class PackageData : public std::enable_shared_from_this<PackageData> {
    public:
        explicit PackageData(SynthUnit *synthUnit) : synthUnit(synthUnit) {
        }

        SynthUnit *synthUnit;
        std::string id;
        stdc::VersionNumber version;
        bool loaded = false;
        DisplayText name;
        DisplayText description;
        DisplayText vendor;
        DisplayText readme;
        DisplayText license;
        std::string url;
        std::filesystem::path path;
        std::vector<PackageDependency> dependencies;
        std::map<std::string, std::shared_ptr<PackageData>, std::less<>> dependencyBindings;
        std::vector<std::unique_ptr<ContribSpec>> ownedContributions;
        std::map<std::string, std::vector<ContribSpec *>, std::less<>> contributions;
        std::map<std::string, std::map<std::string, ContribSpec *, std::less<>>, std::less<>>
            contributionIndex;
    };

}

#endif // SYNTHRT_PACKAGEHANDLE_P_H
