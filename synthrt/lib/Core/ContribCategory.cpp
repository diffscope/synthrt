#include "ContribCategory.h"
#include "ContribCategory_p.h"
#include "ContribHandler.h"

#include <cassert>
#include <mutex>
#include <cstdlib>

#include <stdcorelib/pimpl.h>

#include "ContribHandler.h"
#include "PackageRef_p.h"
#include "SynthUnit_p.h"

namespace srt {

    std::shared_mutex &ContribCategory::Impl::su_mtx() const {
        return static_cast<SynthUnit::Impl *>(handler->SU()->_impl.get())->su_mtx;
    }

    std::vector<ContribSpec *>
        ContribCategory::Impl::findContributes(const ContribLocator &loc) const {
        std::shared_lock<std::shared_mutex> lock(su_mtx());

        // Resolution happens within one category, so a reference naming a different one cannot
        // match here. An empty category means the caller left the kind open, which this lookup
        // answers from its own contributes.
        if (!loc.category().empty() && loc.category() != handler->name()) {
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
        return impl.handler->name();
    }

    SynthUnit *ContribCategory::SU() const {
        stdc_impl_t;
        return impl.handler->SU();
    }

    std::vector<ContribSpec *> ContribCategory::find(const ContribLocator &loc) const {
        stdc_impl_t;
        return impl.findContributes(loc);
    }

    ContribCategory::ContribCategory(ContribHandler *handler)
        : ObjectPool(*new Impl(this, handler)) {
    }

    ContribCategory::ContribCategory(Impl &impl) : ObjectPool(impl) {
    }

}
