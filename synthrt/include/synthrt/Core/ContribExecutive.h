#ifndef SYNTHRT_CORE_CONTRIBEXECUTIVE_H
#define SYNTHRT_CORE_CONTRIBEXECUTIVE_H

#include <memory>
#include <string_view>
#include <vector>
#include <utility>

#include <stdcorelib/adt/vlarray.h>

#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Core/ContribSpecPayload.h>
#include <synthrt/Support/Expected.h>

namespace srt {

    class PackageData;
    class SynthUnit;

    /// Runtime options supplied to a \c ContribExecutiveFactory.
    class ContribRuntimeOptions : public ContribSpecPayload {
    public:
        ~ContribRuntimeOptions() = default;

        /// Returns the interface contract this value belongs to.
        const ContribContract &contract() const override {
            return m_contract;
        }

    protected:
        explicit ContribRuntimeOptions(ContribContract contract) : m_contract(std::move(contract)) {
        }

    private:
        ContribContract m_contract;
    };

    /// A top level executive created from one contribution declaration.
    ///
    /// An executive owns the sessions, tasks, callbacks, and other execution activity below it.
    /// It must be destroyed before its SynthUnit and before every PackageHandle that retains a
    /// dependency of its supervision tree.
    class SYNTHRT_EXPORT ContribExecutive {
    public:
        virtual ~ContribExecutive();

        /// Returns the declaration from which this executive was created.
        ContribSpec &spec() const;

        /// Returns the SynthUnit that owns this executive and its contribution.
        SynthUnit &synthUnit() const;

        /// Returns the executive supervising this executive, or null for a root executive.
        ContribExecutive *parent() const noexcept;

        /// Returns a snapshot of the executives directly supervised by this executive.
        std::vector<ContribExecutive *> children() const;

        /// Requests all activity in this supervision tree to stop.
        ///
        /// This function requests that every executive in the supervision tree stop its own
        /// activity. A successful return means the request reached every activity owner. It does
        /// not prove that an activity has already stopped. Call \c waitForFinished() for that
        /// synchronization. The call is idempotent.
        Expected<void> stop();

        /// Waits until this executive, its descendants, and their callbacks are quiescent.
        ///
        /// This function waits for every executive in the supervision tree. A successful return
        /// does not destroy the executive. The object remains alive and can be reused. Destruction
        /// remains the caller's responsibility.
        Expected<void> waitForFinished();

        /// Casts this executive to a contract specific type.
        ///
        /// The cast is a static_cast. A debug build with RTTI also asserts that the dynamic type
        /// matches.
        ///
        /// \warning The caller must establish the dynamic type from the contribution contract and
        /// provider agreement before calling this function. Passing another derived type results
        /// in undefined behavior.
        SYNTHRT_DECLARE_AS_METHODS(ContribExecutive)

    protected:
        explicit ContribExecutive(ContribSpec &spec);

        /// Transfers \a child into this executive's supervision tree.
        ///
        /// Both executives must belong to the same SynthUnit. A failure destroys \a child. The
        /// returned pointer remains owned by this executive. Deleting that pointer directly
        /// detaches it from this executive before destruction.
        Expected<ContribExecutive *> adoptChild(std::unique_ptr<ContribExecutive> child);

        /// Creates and adopts one child through the import identified by \a role.
        Expected<ContribExecutive *> createChild(std::string_view role,
                                                 const ContribRuntimeOptions &runtimeOptions);

        /// Requests all activity owned by this executive to stop.
        virtual Expected<void> quit() = 0;

        /// Waits until no execution activity remains owned by this executive.
        virtual Expected<void> wait() = 0;

    private:
        ContribSpec *m_spec;
        PackageData *m_package;
        ContribExecutive *m_parent = nullptr;
        std::vector<ContribExecutive *> m_children;
        bool m_destroyingChildren = false;
        bool m_destructionPrepared = false;

        STDC_DISABLE_COPY_MOVE(ContribExecutive)

        friend class PackageData;
    };

    /// Creates executives of one fixed contribution contract.
    class ContribExecutiveFactory {
    public:
        virtual ~ContribExecutiveFactory() = default;

        /// Creates an executive of the fixed contribution contract represented by this factory.
        ///
        /// The implementation must return the concrete executive type required by the target
        /// interface, variant, and Level. Callers may use that contract to downcast the result.
        ///
        /// \warning Returning another dynamic type violates the provider ABI and results in
        /// undefined behavior when the caller performs the contract defined downcast. A debug
        /// build with RTTI asserts that the dynamic type matches.
        virtual Expected<std::unique_ptr<ContribExecutive>>
            create(const ContribRuntimeOptions &runtimeOptions) = 0;

    protected:
        ContribExecutiveFactory() = default;
    };

}

#endif // SYNTHRT_CORE_CONTRIBEXECUTIVE_H
