#include "ContribCategory.h"
#include "ContribCategory_p.h"

#include <cassert>
#include <utility>

namespace srt {

    ContribCategory::~ContribCategory() = default;

    const std::string &ContribCategory::name() const {
        return _impl->name;
    }

    ContribCategory::DeclarationMode ContribCategory::declarationMode() const noexcept {
        return _impl->declarationMode;
    }

    SynthUnit &ContribCategory::synthUnit() const {
        assert(_impl->synthUnit);
        return *_impl->synthUnit;
    }

    std::vector<ContribSpec *> ContribCategory::contributions() const {
        return _impl->contributions;
    }

    ContribCategory::ContribCategory(std::string name, DeclarationMode declarationMode)
        : _impl(std::make_unique<Impl>(std::move(name), declarationMode)) {
    }

}
