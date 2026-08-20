#ifndef SYNTHRT_CONTRIBHANDLER_P_H
#define SYNTHRT_CONTRIBHANDLER_P_H

#include <memory>
#include <string>

#include "ContribHandler.h"

/// \file
/// What a \c ContribHandler keeps: what it was registered as, the unit it serves, and the category
/// it built for callers.
///
/// Nothing here is a stable interface, and all of it may change between any two versions.

namespace srt {

    class SYNTHRT_EXPORT ContribHandler::Impl {
    public:
        Impl() = default;
        virtual ~Impl() = default;

    public:
        std::string name;
        SynthUnit *su = nullptr;
        std::unique_ptr<ContribCategory> category;

        STDC_DISABLE_COPY(Impl)
    };

}

#endif // SYNTHRT_CONTRIBHANDLER_P_H