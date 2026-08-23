#ifndef SYNTHRT_CONTRIBSPEC_P_H
#define SYNTHRT_CONTRIBSPEC_P_H

#include "ContribSpec.h"

#include <utility>
#include <vector>

namespace srt {

    class PackageData;

    class ContribSpec::Import::Impl {
    public:
        Impl(ContribReference reference, JsonValue manifestOptions)
            : reference(std::move(reference)), manifestOptions(std::move(manifestOptions)) {
        }

        ContribReference reference;
        JsonValue manifestOptions;
        std::unique_ptr<ContribImportOptions> options;
    };

    class ContribSpec::Impl {
    public:
        PackageData *package = nullptr;
        ContribReference reference;
        bool hasModuleDeclaration = false;
        DisplayText name;
        std::string interface;
        std::string variant;
        int level = 0;
        JsonValue manifestExports;
        std::unique_ptr<ContribExports> exports;
        JsonValue manifestConfiguration;
        std::unique_ptr<ContribConfiguration> configuration;
        std::vector<Import> imports;
    };

}

#endif // SYNTHRT_CONTRIBSPEC_P_H
