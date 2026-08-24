#ifndef SYNTHRT_CONTRIBIMPORTBINDING_H
#define SYNTHRT_CONTRIBIMPORTBINDING_H

#include <memory>

#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Support/Expected.h>
#include <synthrt/synthrt_global.h>

namespace srt {

    class PackageData;
    class PackageLoader;

    /// A prepared or active runtime connection from one contribution to another.
    class SYNTHRT_EXPORT ContribImportBinding {
    public:
        enum class State {
            Prepared,
            Active,
            Closed,
        };

        virtual ~ContribImportBinding();

        /// Returns the contribution that owns this binding.
        ContribSpec &importer() const;

        /// Returns the manifest import declaration represented by this binding.
        const ContribSpec::Import &declaration() const;

        /// Returns the contribution reached through this binding.
        ContribSpec &target() const;

        /// Returns the target interpreter's typed import options.
        const ContribImportOptions &options() const;

        /// Returns the current binding lifecycle state.
        State state() const noexcept;

        SYNTHRT_DECLARE_AS_METHODS(ContribImportBinding)

    protected:
        ContribImportBinding(ContribSpec &importer, const ContribSpec::Import &declaration,
                             ContribSpec &target, std::unique_ptr<ContribImportOptions> options);

        /// Opens the prepared runtime connection during Commit.
        ///
        /// This operation must not fail, allocate resources, or perform I/O.
        virtual void activate() noexcept = 0;

        /// Prevents both sides from starting new runtime calls through this binding.
        virtual void close() noexcept = 0;

        /// Waits until calls that entered before close have left this binding.
        virtual Expected<void> wait() = 0;

    private:
        void activateForCommit() noexcept;
        void closeForUnload() noexcept;
        Expected<void> waitForUnload();

        class Impl;
        std::unique_ptr<Impl> _impl;

        STDC_DISABLE_COPY_MOVE(ContribImportBinding)

        friend class PackageData;
        friend class PackageLoader;
    };

}

#endif // SYNTHRT_CONTRIBIMPORTBINDING_H
