#ifndef DSINFER_ACOUSTICINFERENCE_H
#define DSINFER_ACOUSTICINFERENCE_H

#include <memory>
#include <shared_mutex>

#include <synthrt/SVS/InferenceExecInstance.h>

namespace ds {

    class InferenceDriver;
    class InferenceSession;

    class AcousticInference : public srt::InferenceExecInstance {
    public:
        explicit AcousticInference(srt::InferenceSpec &spec);
        ~AcousticInference();

    public:
        srt::Expected<void> initialize(const srt::TaskInitArgs &args) override;

        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override;
        srt::Expected<void> startAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                       AsyncCallback callback) override;
        srt::Expected<void> stop() override;
        srt::Expected<void> waitForFinished() override;

    private:
        InferenceDriver *m_driver = nullptr;
        std::unique_ptr<InferenceSession> m_session;
        mutable std::shared_mutex m_mutex;
    };

}

#endif // DSINFER_ACOUSTICINFERENCE_H
