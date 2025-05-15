#ifndef SYNTHRT_INFERENCE_H
#define SYNTHRT_INFERENCE_H

#include <synthrt/Task/ITask.h>

namespace srt {

    class InferenceSpec;

    class SynthUnit;

    class InferenceInitArgs : public TaskInitArgs {
    public:
        InferenceInitArgs(const std::string &name) : TaskInitArgs(name) {
        }

        /// The intermediate output can be stored here in the form of an \a Object for later use.
        NO<ObjectPool> intermediateObjects;
    };

    class SYNTHRT_EXPORT Inference : public ITask {
    public:
        explicit Inference(const InferenceSpec *spec);
        ~Inference();

    public:
        const InferenceSpec *spec() const;
        SynthUnit *SU() const;

    protected:
        class Impl;
    };

}

#endif // SYNTHRT_INFERENCE_H