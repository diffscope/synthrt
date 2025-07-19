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
                                 const srt::NO<InferenceSessionOpenArgs> &args) override;
        srt::Expected<void> close() override;
        bool isOpen() const override;

        int64_t id() const override;

    public:
        srt::Expected<srt::NO<srt::TaskResult>> start(const srt::NO<srt::TaskStartInput> &input) override;
        srt::Expected<void> startAsync(const srt::NO<srt::TaskStartInput> &input,
                                       const StartAsyncCallback &callback) override;
        srt::NO<srt::TaskResult> result() const override;
        bool stop() override;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        friend class OnnxTask;
    };

}

#endif // DSINFER_ONNXSESSION_H