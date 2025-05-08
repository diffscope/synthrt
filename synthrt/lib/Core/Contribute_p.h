#ifndef SYNTHRT_CONTRIBUTE_P_H
#define SYNTHRT_CONTRIBUTE_P_H

#include <map>
#include <list>
#include <unordered_map>
#include <shared_mutex>

#include <synthrt/Support/Error.h>
#include <synthrt/Core/Contribute.h>

#include "NamedObject_p.h"
#include "SynthUnit_p.h"

namespace srt {

    class PackageData;

    class ContribSpec::Impl {
    public:
        explicit Impl(std::string category) : category(std::move(category)), state(Invalid) {
        }
        virtual ~Impl() = default;

    public:
        virtual bool read(const std::filesystem::path &basePath, const JsonObject &obj,
                          Error *error) {
            return false;
        }

    public:
        std::string category;
        std::string id;
        stdc::VersionNumber fmtVersion;

        State state;
        PackageData *package;
    };

    class ContribCategory::Impl : public ObjectPool::Impl {
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

        inline std::shared_mutex &su_mtx() const {
            return static_cast<SynthUnit::Impl *>(su->_impl.get())->su_mtx;
        }

        std::vector<ContribSpec *> findContributes(const ContribLocator &loc) const;
    };

}

#endif // SYNTHRT_CONTRIBUTE_P_H