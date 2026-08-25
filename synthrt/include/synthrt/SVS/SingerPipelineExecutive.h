#ifndef SYNTHRT_SINGERPIPELINEEXECUTIVE_H
#define SYNTHRT_SINGERPIPELINEEXECUTIVE_H

#include <synthrt/Core/ContribExecutive.h>
#include <synthrt/SVS/SingerContrib.h>

namespace srt {

    class SingerPipelineExecutive;

    /// Runtime options supplied when a singer pipeline is created.
    class SingerPipelineRuntimeOptions : public ContribRuntimeOptions {
    public:
        ~SingerPipelineRuntimeOptions() = default;

    protected:
        using ContribRuntimeOptions::ContribRuntimeOptions;
    };

    /// Adds one synthesis pipeline implementation to a loaded SingerSpec.
    class SYNTHRT_EXPORT SingerPipelineExtension : public ContribSpecExtension {
    public:
        ~SingerPipelineExtension();

        /// Returns the singer declaration extended by this object.
        inline SingerSpec &spec() const {
            return *ContribSpecExtension::spec().as<SingerSpec>();
        }

        /// Creates this extension's synthesis pipeline.
        virtual Expected<std::unique_ptr<SingerPipelineExecutive>>
            createPipeline(const SingerPipelineRuntimeOptions &runtimeOptions) = 0;

    protected:
        SingerPipelineExtension(SingerSpec &spec, std::string id);
    };

    /// The provider-defined synthesis pipeline of one loaded singer contribution.
    ///
    /// Contract-specific derived classes expose typed functions that create the inference
    /// executives selected by the singer declaration.
    class SYNTHRT_EXPORT SingerPipelineExecutive : public ContribExecutive {
    public:
        explicit SingerPipelineExecutive(SingerSpec &spec);
        ~SingerPipelineExecutive();

    public:
        inline SingerSpec &spec() const {
            return *ContribExecutive::spec().as<SingerSpec>();
        }

    protected:
        /// Stops runtime activity retained by the singer pipeline.
        ///
        /// The default implementation succeeds because child inference executives are stopped by
        /// the executive supervision tree.
        Expected<void> quit() override;

        /// Waits for runtime activity retained by the singer pipeline.
        ///
        /// The default implementation succeeds because child inference executives are waited by
        /// the executive supervision tree.
        Expected<void> wait() override;
    };

}

#endif // SYNTHRT_SINGERPIPELINEEXECUTIVE_H
