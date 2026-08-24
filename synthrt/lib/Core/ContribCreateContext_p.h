#ifndef SYNTHRT_CONTRIBCREATECONTEXT_P_H
#define SYNTHRT_CONTRIBCREATECONTEXT_P_H

#include "ContribCreateContext.h"

#include <optional>

#include <stdcorelib/adt/vlarray.h>

namespace srt {

    class PackageData;

    class ContribCreateContext::Data {
    public:
        PackageData *package = nullptr;
        ContribReference reference;
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

}

#endif // SYNTHRT_CONTRIBCREATECONTEXT_P_H
