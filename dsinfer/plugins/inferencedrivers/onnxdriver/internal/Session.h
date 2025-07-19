#ifndef DSINFER_ONNXDRIVER_SESSION_H
#define DSINFER_ONNXDRIVER_SESSION_H

#include <map>
#include <memory>
#include <filesystem>
#include <functional>

#include <synthrt/Support/Expected.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <synthrt/Task/ITask.h>


namespace ds::onnxdriver {

    class Session {
    public:
        enum SessionHint {
            SH_NoHint,
            SH_PreferCPUHint = 0x1,
        };

        Session();
        ~Session();

        Session(const Session &) = delete;
        Session &operator=(const Session &) = delete;

        Session(Session &&other) noexcept;
        Session &operator=(Session &&other) noexcept;

    public:
        srt::Expected<void> open(const std::filesystem::path &path, const srt::NO<Api::Onnx::SessionOpenArgs> &args);
        srt::Expected<void> close();

        const std::vector<std::string> &inputNames() const;
        const std::vector<std::string> &outputNames() const;

        srt::Expected<srt::NO<srt::TaskResult>> run(const srt::NO<srt::TaskStartInput> &input);
        srt::Expected<void> runAsync(const srt::NO<srt::TaskStartInput> &input, const srt::ITask::StartAsyncCallback &callback);

        void terminate();

        const std::filesystem::path &path() const;
        bool isOpen() const;

        srt::NO<srt::TaskResult> result() const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // DSINFER_ONNXDRIVER_SESSION_H
