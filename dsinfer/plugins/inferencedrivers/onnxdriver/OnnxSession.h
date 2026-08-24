#ifndef DSINFER_ONNXSESSION_H
#define DSINFER_ONNXSESSION_H

#include <dsinfer/Inference/InferenceSession.h>

namespace ds {

    class OnnxSession : public InferenceSession {
    public:
        OnnxSession();
        ~OnnxSession();

    public:
        srt::Expected<void> open(const std::filesystem::path &path,
                                 const InferenceSessionOpenArgs &args) override;
        srt::Expected<void> close() override;
        bool isOpen() const override;

        int64_t id() const override;

    public:
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                       AsyncCallback callback) override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        friend class OnnxTask;
    };

}

#endif // DSINFER_ONNXSESSION_H
