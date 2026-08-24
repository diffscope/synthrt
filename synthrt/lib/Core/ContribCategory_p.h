#ifndef SYNTHRT_CONTRIBCATEGORY_P_H
#define SYNTHRT_CONTRIBCATEGORY_P_H

#include "ContribCategory.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <cassert>

#include <stdcorelib/adt/vlarray.h>

namespace srt {

    class PackageData;

    class ContribCreateContext::Data {
    public:
        PackageData *package = nullptr;
        ContribLocator locator;
        JsonObject manifestEntry;
        std::optional<std::filesystem::path> declarationPath;
        std::optional<JsonObject> manifestDeclaration;
        DisplayText name;
        std::string interface;
        std::string variant;
        int level = 0;
        JsonValue manifestExports;
        JsonValue manifestConfiguration;
        stdc::vlarray<ContribSpec::Import> imports;
    };

    class ContribCategory::Impl {
    public:
        Impl(std::string name, DeclarationMode declarationMode, std::string interpreterIid)
            : name(std::move(name)), declarationMode(declarationMode),
              interpreterIid(std::move(interpreterIid)) {
            assert(declarationMode != EntryOnly || interpreterIid.empty());
        }

        std::string name;
        DeclarationMode declarationMode;
        std::string interpreterIid;
        SynthUnit *synthUnit = nullptr;
        std::vector<ContribSpec *> contributions;
    };

}

#endif // SYNTHRT_CONTRIBCATEGORY_P_H
