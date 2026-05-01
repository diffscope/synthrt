#ifndef DSINFER_GGMLSESSION_H
#define DSINFER_GGMLSESSION_H

#include <dsinfer/Inference/InferenceSession.h>

namespace ds {

    class GgmlSession : public InferenceSession {
    public:
        GgmlSession();
        ~GgmlSession();

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
    };

}

#endif // DSINFER_GGMLSESSION_H
