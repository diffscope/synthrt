#ifndef SYNTHRT_CONTRIBCATEGORY_P_H
#define SYNTHRT_CONTRIBCATEGORY_P_H

#include "ContribCategory.h"

#include <cassert>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <stdcorelib/adt/vlarray.h>

#include "ContribSpec_p.h"
#include "PackageHandle_p.h"

namespace srt {
    class ContribCreateContext::Data {
    public:
        std::optional<ContribImport> addImport(std::string role, ContribLocator locator,
                                               JsonValue manifestOptions) {
            auto key = role;
            const auto [it, inserted] = importData.try_emplace(
                std::move(key), std::move(role), std::move(locator), std::move(manifestOptions));
            if (!inserted) {
                return std::nullopt;
            }
            return ContribImport(it->second);
        }

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
        stdc::vlarray<ContribImport> imports;
        std::map<std::string, ContribImport::Data, std::less<>> importData;
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
