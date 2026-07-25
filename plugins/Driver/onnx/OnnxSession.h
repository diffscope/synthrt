#pragma once

#include <synthrt/Driver/InferenceSession.h>

namespace srt::driver::onnx {

    class OnnxSession : public srt::driver::InferenceSession {
    public:
        OnnxSession();
        ~OnnxSession();

    public:
        srt::core::Expected<void>
            open(const std::filesystem::path &path,
                 const srt::core::NO<srt::driver::InferenceSessionOpenArgs> &args) override;
        srt::core::Expected<void> close() override;
        bool isOpen() const override;

        int64_t id() const override;

        std::vector<std::string> inputNames() const override;

    public:
        srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
            start(const srt::core::NO<srt::core::TaskStartInput> &input) override;
        srt::core::Expected<void>
            startAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                       const srt::core::ITask::StartAsyncCallback &callback) override;
        srt::core::NO<srt::core::TaskResult> result() const override;
        bool stop() override;

    protected:
        class Impl;
        std::unique_ptr<Impl> m_impl;

        friend class OnnxTask;
    };

}
