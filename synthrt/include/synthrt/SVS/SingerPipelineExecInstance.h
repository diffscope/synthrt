#ifndef SYNTHRT_SINGERPIPELINEEXECINSTANCE_H
#define SYNTHRT_SINGERPIPELINEEXECINSTANCE_H

#include <synthrt/Core/ContribExecInstance.h>

namespace srt {

    class InferenceExecInstance;
    class SingerSpec;

    /// Runtime options supplied when a singer pipeline is created.
    class SingerPipelineRuntimeOptions : public ContribRuntimeOptions {
    public:
        ~SingerPipelineRuntimeOptions() = default;

    protected:
        using ContribRuntimeOptions::ContribRuntimeOptions;
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
        SingerSpec &spec() const;

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
