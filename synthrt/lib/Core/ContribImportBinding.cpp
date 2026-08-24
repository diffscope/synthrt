#include "ContribImportBinding.h"

#include <cassert>
#include <utility>

#include "ContribSpec.h"

namespace srt {

    class ContribImportBinding::Impl {
    public:
        Impl(ContribSpec &importer, const ContribSpec::Import &declaration, ContribSpec &target,
             std::unique_ptr<ContribImportOptions> options)
            : importer(&importer), declaration(&declaration), target(&target),
              options(std::move(options)) {
        }

        ContribSpec *importer;
        const ContribSpec::Import *declaration;
        ContribSpec *target;
        std::unique_ptr<ContribImportOptions> options;
        State state = State::Prepared;
    };

    ContribImportBinding::ContribImportBinding(ContribSpec &importer,
                                               const ContribSpec::Import &declaration,
                                               ContribSpec &target,
                                               std::unique_ptr<ContribImportOptions> options)
        : _impl(std::make_unique<Impl>(importer, declaration, target, std::move(options))) {
        assert(_impl->options);
    }

    ContribImportBinding::~ContribImportBinding() = default;

    ContribSpec &ContribImportBinding::importer() const {
        return *_impl->importer;
    }

    const ContribSpec::Import &ContribImportBinding::declaration() const {
        return *_impl->declaration;
    }

    ContribSpec &ContribImportBinding::target() const {
        return *_impl->target;
    }

    const ContribImportOptions &ContribImportBinding::options() const {
        return *_impl->options;
    }

    ContribImportBinding::State ContribImportBinding::state() const noexcept {
        return _impl->state;
    }

    void ContribImportBinding::activateForCommit() noexcept {
        assert(_impl->state == State::Prepared);
        activate();
        _impl->state = State::Active;
    }

    void ContribImportBinding::closeForUnload() noexcept {
        assert(_impl->state == State::Active);
        close();
        _impl->state = State::Closed;
    }

    Expected<void> ContribImportBinding::waitForUnload() {
        assert(_impl->state == State::Closed);
        return wait();
    }

}
