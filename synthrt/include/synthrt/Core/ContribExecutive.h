#ifndef SYNTHRT_CONTRIBEXECUTIVE_H
#define SYNTHRT_CONTRIBEXECUTIVE_H

#include <atomic>
#include <memory>
#include <string_view>
#include <vector>

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

    protected:
        using ContribSpecPayload::ContribSpecPayload;
    };

    /// A top level executive created from one contribution declaration.
    ///
    /// An executive owns the sessions, tasks, callbacks, and other execution activity below it.
    /// It must be destroyed before the Package containing its contribution is released.
    class SYNTHRT_EXPORT ContribExecutive {
    public:
        enum LifecycleState {
            Running,
            Stopping,
            Stopped,
        };

        virtual ~ContribExecutive();

        /// Returns the declaration from which this executive was created.
        ContribSpec &spec() const;

        /// Returns the SynthUnit that owns this executive and its contribution.
        SynthUnit &synthUnit() const;

        /// Returns whether this executive accepts work, is stopping, or has stopped.
        LifecycleState lifecycleState() const noexcept;

        /// Returns the executive supervising this executive, or null for a root executive.
        ContribExecutive *parent() const noexcept;

        /// Returns a snapshot of the executives directly supervised by this executive.
        std::vector<ContribExecutive *> children() const;

        /// Casts this executive to a contract specific type without a runtime check.
        ///
        /// \warning The caller must establish the dynamic type from the contribution contract and
        /// provider agreement before calling this function. Passing another derived type results
        /// in undefined behavior.
        SYNTHRT_DECLARE_AS_METHODS(ContribExecutive)

    protected:
        explicit ContribExecutive(ContribSpec &spec);

        /// Transfers \a child into this executive's supervision tree.
        ///
        /// Both executives must belong to the same SynthUnit and must still be running. A failure
        /// destroys \a child. The returned pointer remains owned by this executive. Deleting that
        /// pointer directly detaches it from this executive before destruction.
        Expected<ContribExecutive *> adoptChild(std::unique_ptr<ContribExecutive> child);

        /// Creates and adopts one child through the import identified by \a role.
        Expected<ContribExecutive *> createChild(std::string_view role,
                                                 const ContribRuntimeOptions &runtimeOptions);

        /// Requests all activity owned by this executive to stop.
        virtual Expected<void> quit() = 0;

        /// Waits until no execution activity remains owned by this executive.
        virtual Expected<void> wait() = 0;

    private:
        Expected<void> quitForUnload();
        Expected<void> waitForUnload();

        ContribSpec *m_spec;
        PackageData *m_package;
        ContribExecutive *m_parent = nullptr;
        std::vector<ContribExecutive *> m_children;
        bool m_destroyingChildren = false;
        std::atomic<LifecycleState> m_state = LifecycleState::Running;

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
        /// interface, variant, and Level. Callers may use that contract to perform an unchecked
        /// downcast.
        ///
        /// \warning Returning another dynamic type violates the provider ABI and results in
        /// undefined behavior when the caller performs the contract defined downcast.
        virtual Expected<std::unique_ptr<ContribExecutive>>
            create(const ContribRuntimeOptions &runtimeOptions) = 0;

    protected:
        ContribExecutiveFactory() = default;
    };

}

#endif // SYNTHRT_CONTRIBEXECUTIVE_H
