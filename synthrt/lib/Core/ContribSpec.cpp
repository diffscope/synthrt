#include "ContribSpec.h"
#include "ContribSpec_p.h"

#include <stdcorelib/pimpl.h>

#include "PackageRef_p.h"

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

    ContribSpec::ContribSpec(std::string category) : _impl(new Impl(std::move(category))) {
    }

    ContribSpec::ContribSpec(Impl &impl) : _impl(&impl) {
    }

}
