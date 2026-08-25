#ifndef SYNTHRT_CONTRIBSPEC_P_H
#define SYNTHRT_CONTRIBSPEC_P_H

#include "ContribSpec.h"

#include <map>
#include <utility>

#include <stdcorelib/adt/vlarray.h>
#include <stdcorelib/plugin/pluginloader.h>

#include "ContribExecInstance.h"
#include "ContribImportBinding.h"
#include "ContribInterpreter.h"
#include "PackageHandle_p.h"

namespace srt {

    class ContribImport::Data {
    public:
        Data(std::string role, ContribLocator locator, JsonValue manifestOptions)
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
        stdc::vlarray<ContribImport> imports;
        std::map<std::string, ContribImport::Data, std::less<>> importData;
    };

}

#endif // SYNTHRT_CONTRIBSPEC_P_H
