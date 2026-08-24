#ifndef SYNTHRT_INFERENCE_H
#define SYNTHRT_INFERENCE_H

#include <synthrt/Core/ContribExecInstance.h>
#include <synthrt/Task/ITask.h>

namespace srt {

    class InferenceSpec;

    class SynthUnit;

    class InferenceInitArgs : public TaskInitArgs {
    public:
        InferenceInitArgs(std::string name) : TaskInitArgs(std::move(name)) {
        }

        /// The intermediate output can be stored here in the form of an \c NamedObject for later
        /// use.
        NO<ObjectPool> intermediateObjects;
    };

    class SYNTHRT_EXPORT Inference : public ITask, public ContribExecInstance {
    public:
        explicit Inference(InferenceSpec &spec);
        ~Inference();

    public:
        InferenceSpec &spec() const;
        SynthUnit &synthUnit() const;

    protected:
        class Impl;
    };

}

#endif // SYNTHRT_INFERENCE_H
