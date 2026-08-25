#ifndef SYNTHRT_CONTRIBSPEC_P_H
#define SYNTHRT_CONTRIBSPEC_P_H

#include "ContribSpec.h"

#include <utility>

#include <stdcorelib/adt/vlarray.h>

#include "ContribExecInstance.h"
#include "ContribImportBinding.h"

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
        Impl(std::string role, ContribLocator locator, JsonValue manifestOptions)
            : role(std::move(role)), locator(std::move(locator)),
              manifestOptions(std::move(manifestOptions)) {
        }

        std::string role;
        ContribLocator locator;
        JsonValue manifestOptions;
        std::unique_ptr<ContribImportOptions> options;
        std::unique_ptr<ContribImportBinding> binding;
        std::unique_ptr<ContribExecFactory> execFactory;
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
