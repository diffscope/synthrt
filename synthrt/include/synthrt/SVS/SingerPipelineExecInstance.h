#ifndef SYNTHRT_SINGERPIPELINEEXECINSTANCE_H
#define SYNTHRT_SINGERPIPELINEEXECINSTANCE_H

#include <synthrt/Core/ContribExecInstance.h>
#include <synthrt/SVS/SingerContrib.h>

namespace srt {

    class SingerPipelineExecInstance;

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
        virtual Expected<std::unique_ptr<SingerPipelineExecInstance>>
            createPipeline(const SingerPipelineRuntimeOptions &runtimeOptions) = 0;

    protected:
        SingerPipelineExtension(SingerSpec &spec, std::string id);
    };

    /// The provider-defined synthesis pipeline of one loaded singer contribution.
    ///
    /// Contract-specific derived classes expose typed functions that create the inference
    /// instances selected by the singer declaration.
    class SYNTHRT_EXPORT SingerPipelineExecInstance : public ContribExecInstance {
    public:
        explicit SingerPipelineExecInstance(SingerSpec &spec);
        ~SingerPipelineExecInstance();

    public:
        inline SingerSpec &spec() const {
            return *ContribExecInstance::spec().as<SingerSpec>();
        }

    protected:
        /// Stops runtime activity retained by the singer pipeline.
        ///
        /// The default implementation succeeds because child inference instances are stopped by
        /// the execution instance supervision tree.
        Expected<void> quit() override;

        /// Waits for runtime activity retained by the singer pipeline.
        ///
        /// The default implementation succeeds because child inference instances are waited by
        /// the execution instance supervision tree.
        Expected<void> wait() override;
    };

}

#endif // SYNTHRT_SINGERPIPELINEEXECINSTANCE_H
