#ifndef ONNXSESSION_H
#define ONNXSESSION_H

#include <dsinfer/Inference/InferenceSession.h>

namespace ds {

    class OnnxSession : public InferenceSession {
    public:
        OnnxSession();
        ~OnnxSession();

    public:
        bool open(const std::filesystem::path &path, const srt::NO<InferenceSessionOpenArgs> &args,
                  srt::Error *error) override;
        bool close(srt::Error *error) override;
        bool isOpen() const override;

        int64_t id() const override;

    public:
        bool start(const srt::NO<srt::TaskStartInput> &input, srt::Error *error) override;
        bool startAsync(const srt::NO<srt::TaskStartInput> &input,
                        const StartAsyncCallback &callback, srt::Error *error) override;
        srt::NO<srt::TaskResult> result() const override;
        bool stop() override;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        friend class OnnxTask;
    };

}

#endif // ONNXSESSION_H
