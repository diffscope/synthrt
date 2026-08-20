#include "ContribHandler.h"
#include "ContribHandler_p.h"

#include <cassert>
#include <mutex>
#include <cstdlib>

#include <stdcorelib/pimpl.h>

#include "ContribCategory_p.h"
#include "ContribSpec_p.h"
#include "PackageRef_p.h"
#include "SynthUnit_p.h"

namespace srt {

    ContribHandler::ContribHandler() : _impl(new Impl()) {
    }

    ContribHandler::~ContribHandler() = default;

    const std::string &ContribHandler::name() const {
        stdc_impl_t;
        return impl.name;
    }

    ContribCategory *ContribHandler::category() const {
        stdc_impl_t;
        return impl.category.get();
    }

    SynthUnit *ContribHandler::SU() const {
        stdc_impl_t;
        return impl.su;
    }

    ContribCategory *ContribHandler::createCategory() {
        return new ContribCategory(this);
    }

    Expected<void> ContribHandler::loadSpec(ContribSpec *spec, ContribSpec::State state) {
        // The index lives with the category, and keeping it right is the framework's half of
        // loading. An override does its own work and chains here.
        auto &impl = *static_cast<ContribCategory::Impl *>(category()->_impl.get());

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


}

// Owns the handler list. Exported so that the head and tail a caller outside this library reaches
// through ContribHandlerRegistry are the ones the registrations actually filled in, rather than an
// empty list of its own. Has to sit at global scope so that it can name stdc.
STDC_INSTANTIATE_STATIC_REGISTRY_EXPORT(srt::ContribHandler, SYNTHRT_EXPORT)
