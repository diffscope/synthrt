#ifndef SYNTHRT_CONTRIBSPEC_P_H
#define SYNTHRT_CONTRIBSPEC_P_H

#include <memory>
#include <string>

#include "ContribSpec.h"

/// \file
/// What a \c ContribSpec keeps for the framework: which package it came from, what that package
/// calls it, and how far it has been loaded. A kind's own state does not live here -- that goes
/// wherever the kind's author puts it.
///
/// Nothing here is a stable interface, and all of it may change between any two versions.

namespace srt {

    class PackageData;

    class SYNTHRT_EXPORT ContribSpec::Impl {
    public:
        explicit Impl(std::string category) : category(std::move(category)), state(Invalid) {
        }
        virtual ~Impl() = default;

    public:
        std::string category;
        std::string id;

        State state;
        PackageData *package;

        STDC_DISABLE_COPY(Impl)
    };

}

#endif // SYNTHRT_CONTRIBSPEC_P_H
