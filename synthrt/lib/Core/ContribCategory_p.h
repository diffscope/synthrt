#ifndef SYNTHRT_CONTRIBCATEGORY_P_H
#define SYNTHRT_CONTRIBCATEGORY_P_H

#include <map>
#include <list>
#include <unordered_map>
#include <shared_mutex>

#include "ContribCategory.h"
#include "ContribHandler.h"
#include "ContribSpec_p.h"
#include "NamedObject_p.h"

/// \file
/// What a \c ContribCategory keeps for the framework: the contributes of its kind and the index
/// a reference is resolved through.
///
/// Nothing here is a stable interface, and all of it may change between any two versions.

namespace srt {

    class SYNTHRT_EXPORT ContribCategory::Impl : public ObjectPool::Impl {
    public:
        explicit Impl(ContribCategory *decl, ContribHandler *handler)
            : ObjectPool::Impl(decl), handler(handler) {
        }
        virtual ~Impl() = default;

    public:
        /// The other half of this kind. It built this category and outlives it, and is where the
        /// name and the unit are kept.
        ContribHandler *handler;

        std::list<ContribSpec *> contributes;
        std::map<std::string,
                 std::unordered_map<stdc::VersionNumber,
                                    std::map<std::string, decltype(contributes)::iterator>>>
            indexes;

        /// The lock guarding every category of \a su, held whenever \c contributes or \c indexes
        /// is touched.
        ///
        /// \note Out of line on purpose. Reaching the mutex means naming \c SynthUnit::Impl, and
        ///       that one stays private - a category outside synthrt has no business seeing the
        ///       package tables.
        std::shared_mutex &su_mtx() const;

        std::vector<ContribSpec *> findContributes(const ContribLocator &loc) const;
    };

}

#endif // SYNTHRT_CONTRIBCATEGORY_P_H
