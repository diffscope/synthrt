#ifndef SYNTHRT_SINGEREXECINSTANCE_H
#define SYNTHRT_SINGEREXECINSTANCE_H

#include <memory>

#include <synthrt/Core/ContribExecInstance.h>

namespace srt {

    class ContribImportBinding;
    class InferenceExecInstance;
    class InferenceRuntimeOptions;
    class SingerSpec;

    /// A loaded runtime instance of one singer contribution.
    ///
    /// Contract-specific derived classes expose typed functions that create the inference
    /// instances selected by the singer declaration.
    class SYNTHRT_EXPORT SingerExecInstance : public ContribExecInstance {
    public:
        explicit SingerExecInstance(SingerSpec &spec);
        ~SingerExecInstance();

    public:
        SingerSpec &spec() const;

    protected:
        /// Creates an inference instance through one of this singer's active import bindings.
        Expected<std::unique_ptr<InferenceExecInstance>>
            createInference(ContribImportBinding &binding,
                            const InferenceRuntimeOptions &runtimeOptions);

        /// Stops runtime activity retained by the singer instance.
        ///
        /// The default implementation succeeds because created inference instances are owned by
        /// their callers.
        Expected<void> quit() override;

        /// Waits for runtime activity retained by the singer instance.
        ///
        /// The default implementation succeeds because created inference instances are owned by
        /// their callers.
        Expected<void> wait() override;
    };

}

#endif // SYNTHRT_SINGEREXECINSTANCE_H
