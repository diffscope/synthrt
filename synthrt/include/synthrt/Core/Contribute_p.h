#ifndef SYNTHRT_CONTRIBUTE_P_H
#define SYNTHRT_CONTRIBUTE_P_H

#include <map>
#include <list>
#include <unordered_map>
#include <shared_mutex>

#include <synthrt/Support/Expected.h>
#include <synthrt/Core/Contribute.h>
#include <synthrt/Core/NamedObject_p.h>

/// \file
/// The implementation classes behind \c ContribSpec and \c ContribCategory.
///
/// A contribute category is written by deriving from all four: a category from \c ContribCategory
/// and \c ContribCategory::Impl, and the specs it parses from \c ContribSpec and
/// \c ContribSpec::Impl. That is why this header sits among the public ones, and it is the whole
/// reason - nothing here is a stable interface, and all of it may change between any two versions.
///
/// \sa ContribCategoryRegistry, which the category registers its factory with.

namespace srt {

    class PackageData;

    class SYNTHRT_EXPORT ContribSpec::Impl {
    public:
        explicit Impl(std::string category) : category(std::move(category)), state(Invalid) {
        }
        virtual ~Impl() = default;

    public:
        virtual Expected<void> read(const std::filesystem::path &basePath, const JsonObject &obj) {
            return Error(Error::NotImplemented);
        }

    public:
        std::string category;
        std::string id;
        stdc::VersionNumber fmtVersion;

        State state;
        PackageData *package;
    };

    class SYNTHRT_EXPORT ContribCategory::Impl : public ObjectPool::Impl {
    public:
        explicit Impl(ContribCategory *decl, std::string name, SynthUnit *su)
            : ObjectPool::Impl(decl), name(std::move(name)), su(su) {
        }
        virtual ~Impl() = default;

    public:
        std::string name;
        SynthUnit *su;

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

#endif // SYNTHRT_CONTRIBUTE_P_H
