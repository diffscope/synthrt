#ifndef SYNTHRT_CONTRIBEXECINSTANCE_H
#define SYNTHRT_CONTRIBEXECINSTANCE_H

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

    /// Runtime options supplied to a contribution execution factory.
    class ContribRuntimeOptions : public ContribSpecPayload {
    public:
        ~ContribRuntimeOptions() = default;

    protected:
        using ContribSpecPayload::ContribSpecPayload;
    };

    /// A top level execution instance created from one contribution declaration.
    ///
    /// An instance owns the sessions, tasks, callbacks, and other execution activity below it.
    /// It must be destroyed before the Package containing its contribution is released.
    class SYNTHRT_EXPORT ContribExecInstance {
    public:
        enum LifecycleState {
            Running,
            Stopping,
            Stopped,
        };

        virtual ~ContribExecInstance();

        /// Returns the declaration from which this instance was created.
        ContribSpec &spec() const;

        /// Returns the SynthUnit that owns this instance and its contribution.
        SynthUnit &synthUnit() const;

        /// Returns whether this instance accepts work, is stopping, or has stopped.
        LifecycleState lifecycleState() const noexcept;

        /// Returns the execution instance supervising this instance, or null for a root instance.
        ContribExecInstance *parent() const noexcept;

        /// Returns a snapshot of the execution instances directly supervised by this instance.
        std::vector<ContribExecInstance *> children() const;

        SYNTHRT_DECLARE_AS_METHODS(ContribExecInstance)

    protected:
        explicit ContribExecInstance(ContribSpec &spec);

        /// Transfers \a child into this instance's supervision tree.
        ///
        /// Both instances must belong to the same SynthUnit and must still be running. A failure
        /// destroys \a child. The returned pointer remains owned by this instance. Deleting that
        /// pointer directly detaches it from this instance before destruction.
        Expected<ContribExecInstance *> adoptChild(std::unique_ptr<ContribExecInstance> child);

        /// Creates and adopts one child through the import identified by \a role.
        Expected<ContribExecInstance *> createChild(std::string_view role,
                                                    const ContribRuntimeOptions &runtimeOptions);

        /// Requests all activity owned by this instance to stop.
        virtual Expected<void> quit() = 0;

        /// Waits until no execution activity remains owned by this instance.
        virtual Expected<void> wait() = 0;

    private:
        Expected<void> quitForUnload();
        Expected<void> waitForUnload();

        ContribSpec *m_spec;
        PackageData *m_package;
        ContribExecInstance *m_parent = nullptr;
        stdc::vlarray<ContribExecInstance *> m_children;
        bool m_destroyingChildren = false;
        std::atomic<LifecycleState> m_state = LifecycleState::Running;

        STDC_DISABLE_COPY(ContribExecInstance)

        friend class PackageData;
    };

    /// Creates execution instances of one fixed contribution contract.
    class ContribExecFactory {
    public:
        virtual ~ContribExecFactory() = default;

        virtual Expected<std::unique_ptr<ContribExecInstance>>
            create(const ContribRuntimeOptions &runtimeOptions) = 0;

    protected:
        ContribExecFactory() = default;
    };

}

#endif // SYNTHRT_CONTRIBEXECINSTANCE_H
