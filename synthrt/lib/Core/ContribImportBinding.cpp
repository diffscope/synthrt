#include "ContribImportBinding.h"

#include <cassert>
#include <utility>

#include "ContribSpec.h"

namespace srt {

    ContribImportBinding::ContribImportBinding(ContribSpec &importer,
                                               const ContribImport &declaration,
                                               ContribSpec &target,
                                               std::unique_ptr<ContribImportOptions> options)
        : m_importer(&importer), m_declaration(&declaration), m_target(&target),
          m_options(std::move(options)) {
        assert(m_options);
    }

    ContribImportBinding::~ContribImportBinding() = default;

    ContribSpec &ContribImportBinding::importer() const {
        return *m_importer;
    }

    const ContribImport &ContribImportBinding::declaration() const {
        return *m_declaration;
    }

    ContribSpec &ContribImportBinding::target() const {
        return *m_target;
    }

    const ContribImportOptions &ContribImportBinding::options() const {
        return *m_options;
    }

    ContribImportBinding::State ContribImportBinding::state() const noexcept {
        return m_state;
    }

    void ContribImportBinding::activateForCommit() noexcept {
        assert(m_state == State::Prepared);
        activate();
        m_state = State::Active;
    }

    void ContribImportBinding::closeForUnload() noexcept {
        assert(m_state == State::Active);
        close();
        m_state = State::Closed;
    }

    Expected<void> ContribImportBinding::waitForUnload() {
        assert(m_state == State::Closed);
        return wait();
    }

}
