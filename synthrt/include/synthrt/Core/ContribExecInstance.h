#ifndef SYNTHRT_CONTRIBEXECINSTANCE_H
#define SYNTHRT_CONTRIBEXECINSTANCE_H

#include <atomic>

#include <synthrt/Support/Expected.h>
#include <synthrt/synthrt_global.h>

namespace srt {

    class ContribSpec;
    class PackageData;
    class SynthUnit;

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

        SYNTHRT_DECLARE_AS_METHODS(ContribExecInstance)

    protected:
        explicit ContribExecInstance(ContribSpec &spec);

        /// Requests all activity owned by this instance to stop.
        virtual Expected<void> quit() = 0;

        /// Waits until no execution activity remains owned by this instance.
        virtual Expected<void> wait() = 0;

    private:
        Expected<void> quitForUnload();
        Expected<void> waitForUnload();

        ContribSpec *m_spec;
        PackageData *m_package;
        std::atomic<LifecycleState> m_state = LifecycleState::Running;

        STDC_DISABLE_COPY(ContribExecInstance)

        friend class PackageData;
    };

}

#endif // SYNTHRT_CONTRIBEXECINSTANCE_H
