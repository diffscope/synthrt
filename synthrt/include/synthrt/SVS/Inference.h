#ifndef SYNTHRT_INFERENCE_H
#define SYNTHRT_INFERENCE_H

#include <synthrt/Core/ContribExecInstance.h>
#include <synthrt/Task/ITask.h>

namespace srt {

    class InferenceSpec;

    class InferenceInitArgs : public TaskInitArgs {
    public:
        InferenceInitArgs(std::string type, int version) : TaskInitArgs(std::move(type), version) {
        }
    };

    class SYNTHRT_EXPORT Inference : public ITask, public ContribExecInstance {
    public:
        explicit Inference(InferenceSpec &spec);
        ~Inference();

    public:
        InferenceSpec &spec() const;
    };

}

#endif // SYNTHRT_INFERENCE_H
