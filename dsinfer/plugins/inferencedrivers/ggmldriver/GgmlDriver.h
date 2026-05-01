#ifndef DSINFER_GGMLDRIVER_H
#define DSINFER_GGMLDRIVER_H

#include <dsinfer/Inference/InferenceDriver.h>

namespace ds {

    class GgmlDriver : public InferenceDriver {
    public:
        GgmlDriver();
        ~GgmlDriver();

    public:
        std::string arch() const override;
        std::string backend() const override;

        srt::Expected<void> initialize(const srt::NO<InferenceDriverInitArgs> &args) override;
        srt::NO<InferenceSession> createSession() override;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // DSINFER_GGMLDRIVER_H
