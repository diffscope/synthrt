#ifndef SYNTHRT_SYNTHUNIT_P_H
#define SYNTHRT_SYNTHUNIT_P_H

#include "SynthUnit.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ContribCategory.h"
#include "ContribPluginFactory_p.h"
#include "PackageHandle_p.h"

namespace srt {

    class SynthUnit::Impl {
    public:
        bool packageLoadingBegun = false;
        ContribPluginFactory pluginFactory;
        std::vector<std::filesystem::path> packagePaths;
        std::map<std::string, std::vector<std::filesystem::path>, std::less<>> pluginPaths;
        std::map<std::string, std::unique_ptr<ContribCategory>, std::less<>> categories;
        std::map<std::string,
                 std::map<stdc::VersionNumber, std::shared_ptr<PackageData>, std::less<>>,
                 std::less<>>
            packages;
    };

}

#endif // SYNTHRT_SYNTHUNIT_P_H
