#include "Contribute.h"
#include "Contribute_p.h"

#include <cassert>
#include <utility>
#include <mutex>
#include <cstdlib>

#include <stdcorelib/str.h>
#include <stdcorelib/pimpl.h>

#include "PackageRef_p.h"
#include "SynthUnit_p.h"

namespace srt {

    ContribSpec::~ContribSpec() = default;

    const std::string &ContribSpec::category() const {
        stdc_impl_t;
        return impl.category;
    }

    const std::string &ContribSpec::id() const {
        stdc_impl_t;
        return impl.id;
    }

    ContribSpec::State ContribSpec::state() const {
        stdc_impl_t;
        return impl.state;
    }

    PackageRef ContribSpec::parent() const {
        stdc_impl_t;
        return PackageRef(impl.package);
    }

    SynthUnit *ContribSpec::SU() const {
        stdc_impl_t;
        return impl.package->su;
    }

    ContribSpec::ContribSpec(std::string category, std::unique_ptr<ContribSpecHandler> handler)
        : _impl(new Impl(std::move(category), std::move(handler))) {
    }

    ContribSpecHandler *ContribSpec::handlerObject() {
        stdc_impl_t;
        return impl.handler.get();
    }

    const ContribSpecHandler *ContribSpec::handlerObject() const {
        stdc_impl_t;
        return impl.handler.get();
    }

    ContribSpecHandler::ContribSpecHandler() = default;

    ContribSpecHandler::~ContribSpecHandler() = default;

    std::shared_mutex &ContribCategory::Impl::su_mtx() const {
        return static_cast<SynthUnit::Impl *>(su->_impl.get())->su_mtx;
    }

    std::vector<ContribSpec *>
        ContribCategory::Impl::findContributes(const ContribLocator &loc) const {
        std::shared_lock<std::shared_mutex> lock(su_mtx());

        // Resolution happens within one category, so a reference naming a different one cannot
        // match here. An empty category means the caller left the kind open, which this lookup
        // answers from its own contributes.
        if (!loc.category().empty() && loc.category() != name) {
            return {};
        }

        // The package and version are filled in before a reference reaches this point, by
        // "Fix imports" for singer imports and by the caller otherwise.
        if (loc.package().empty() || loc.version().isEmpty()) {
            return {};
        }
        auto it = indexes.find(loc.package());
        if (it == indexes.end()) {
            return {};
        }
        const auto &versionMap = it->second;

        auto it2 = versionMap.find(loc.version());
        if (it2 == versionMap.end()) {
            return {};
        }
        const auto &inferenceMap = it2->second;

        if (!loc.id().empty()) {
            auto it3 = inferenceMap.find(loc.id());
            if (it3 == inferenceMap.end()) {
                return {};
            }
            return {*it3->second};
        }

        std::vector<ContribSpec *> res;
        res.reserve(inferenceMap.size());
        for (const auto &pair : inferenceMap) {
            res.push_back(*pair.second);
        }
        return res;
    }

    ContribCategory::~ContribCategory() = default;

    const std::string &ContribCategory::name() const {
        stdc_impl_t;
        return impl.name;
    }

    SynthUnit *ContribCategory::SU() const {
        stdc_impl_t;
        return impl.su;
    }

    Expected<void> ContribCategory::loadSpec(ContribSpec *spec, ContribSpec::State state) {
        stdc_impl_t;

        auto spec_impl = spec->_impl.get();
        switch (state) {
            case ContribSpec::Initialized: {
                std::unique_lock<std::shared_mutex> lock(impl.su_mtx());
                auto lib = spec_impl->package;
                auto it = impl.contributes.insert(impl.contributes.end(), spec);
                impl.indexes[lib->id][lib->version][spec_impl->id] = it;
                return Expected<void>();
            }

            case ContribSpec::Ready:
            case ContribSpec::Finished: {
                return Expected<void>();
            }

            case ContribSpec::Deleted: {
                std::unique_lock<std::shared_mutex> lock(impl.su_mtx());
                auto lib = spec_impl->package;
                auto it = impl.indexes.find(lib->id);
                if (it == impl.indexes.end()) {
                    return Expected<void>();
                }
                auto &versionMap = it->second;
                auto it2 = versionMap.find(lib->version);
                if (it2 == versionMap.end()) {
                    return Expected<void>();
                }
                auto &inferenceMap = it2->second;
                auto it3 = inferenceMap.find(spec_impl->id);
                if (it3 == inferenceMap.end()) {
                    return Expected<void>();
                }
                impl.contributes.erase(it3->second);
                inferenceMap.erase(it3);
                if (inferenceMap.empty()) {
                    versionMap.erase(it2);
                    if (versionMap.empty()) {
                        impl.indexes.erase(it);
                    }
                }
                return Expected<void>();
            }
            default:
                break;
        }
        std::abort();
        return Expected<void>();
    }

    std::vector<ContribSpec *> ContribCategory::find(const ContribLocator &loc) const {
        stdc_impl_t;
        return impl.findContributes(loc);
    }

    ContribCategory::ContribCategory(Impl &impl) : ObjectPool(impl) {
    }

    ContribCategory::ContribCategory(std::string name, SynthUnit *su)
        : ObjectPool(*new Impl(this, std::move(name), su)) {
        // The name appears in a reference between the ":" and the "/", so anything outside a
        // segment would produce references that cannot be parsed back. A category comes from
        // whoever registered its factory, which is why this is checked rather than assumed.
        assert(ContribLocator::isValidSegment(ContribCategory::name()) &&
               "a contribute category name must match ^[A-Za-z0-9_-]+$");
    }

}

// Owns the category list. Exported so that the head and tail a caller outside this library reaches
// through ContribCategoryRegistry are the ones the registrations actually filled in, rather than an
// empty list of its own. Has to sit at global scope so that it can name stdc.
STDC_INSTANTIATE_STATIC_REGISTRY_EXPORT(srt::ContribCategoryFactoryBase, SYNTHRT_EXPORT)
