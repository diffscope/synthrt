#ifndef SYNTHRT_CONTRIBSPEC_P_H
#define SYNTHRT_CONTRIBSPEC_P_H

#include "ContribSpec.h"

#include <utility>

#include <stdcorelib/adt/vlarray.h>

namespace srt {

    class ContribInterpreter;
    class PackageData;

}

namespace stdc::plugin {

    class PluginLoader;

}

namespace srt {

    class ContribSpec::Import::Impl {
    public:
        Impl(ContribLocator locator, JsonValue manifestOptions)
            : locator(std::move(locator)), manifestOptions(std::move(manifestOptions)) {
        }

        ContribLocator locator;
        JsonValue manifestOptions;
        std::unique_ptr<ContribImportOptions> options;
    };

    class ContribSpec::Impl {
    public:
        PackageData *package = nullptr;
        ContribLocator locator;
        bool hasModuleDeclaration = false;
        stdc::plugin::PluginLoader *pluginLoader = nullptr;
        ContribInterpreter *interpreter = nullptr;
        JsonObject manifestDeclaration;
        DisplayText name;
        std::string interface;
        std::string variant;
        int level = 0;
        JsonValue manifestExports;
        std::unique_ptr<ContribExports> exports;
        JsonValue manifestConfiguration;
        std::unique_ptr<ContribConfiguration> configuration;
        stdc::vlarray<Import> imports;
    };

}

#endif // SYNTHRT_CONTRIBSPEC_P_H
