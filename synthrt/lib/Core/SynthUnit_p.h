#ifndef SYNTHRT_SYNTHUNIT_P_H
#define SYNTHRT_SYNTHUNIT_P_H

#include "SynthUnit.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ContribCategory.h"
#include "ContribPluginFactory_p.h"
#include "PackageHandle_p.h"
#include "RuntimeService.h"

namespace srt {

    class SynthUnit::Impl {
    public:
        bool packageLoadingBegun = false;
        mutable std::recursive_mutex loadMutex;
        ContribPluginFactory pluginFactory;
        std::vector<std::filesystem::path> packagePaths;
        std::map<std::string, std::vector<std::filesystem::path>, std::less<>> pluginPaths;
        std::map<std::string, std::map<std::string, std::unique_ptr<RuntimeService>, std::less<>>,
                 std::less<>>
            runtimeServices;
        std::map<std::string, std::unique_ptr<ContribCategory>, std::less<>> categories;
        std::map<std::string,
                 std::map<stdc::VersionNumber, std::weak_ptr<PackageData>, std::less<>>,
                 std::less<>>
            packages;
    };

}

#endif // SYNTHRT_SYNTHUNIT_P_H
