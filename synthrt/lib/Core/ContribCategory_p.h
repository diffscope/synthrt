#ifndef SYNTHRT_CONTRIBCATEGORY_P_H
#define SYNTHRT_CONTRIBCATEGORY_P_H

#include "ContribCategory.h"

#include <string>
#include <utility>
#include <vector>

namespace srt {

    class ContribCategory::Impl {
    public:
        Impl(std::string name, DeclarationMode declarationMode, std::string interpreterIid)
            : name(std::move(name)), declarationMode(declarationMode),
              interpreterIid(std::move(interpreterIid)) {
        }

        std::string name;
        DeclarationMode declarationMode;
        std::string interpreterIid;
        SynthUnit *synthUnit = nullptr;
        std::vector<ContribSpec *> contributions;
    };

}

#endif // SYNTHRT_CONTRIBCATEGORY_P_H
