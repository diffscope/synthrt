#ifndef SRT_DRIVER_ONNX_SESSION_H
#define SRT_DRIVER_ONNX_SESSION_H

#include <map>
#include <memory>
#include <filesystem>
#include <functional>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <synthrt/Core/Task/ITask.h>


namespace srt::driver::onnx {

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
        srt::core::Expected<void> open(const std::filesystem::path &path,
                                       const srt::core::NO<SessionOpenArgs> &args);
        srt::core::Expected<void> close();

        const std::vector<std::string> &inputNames() const;
        const std::vector<std::string> &outputNames() const;

        srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
            run(const srt::core::NO<srt::core::TaskStartInput> &input);
        srt::core::Expected<void>
            runAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                     const srt::core::ITask::StartAsyncCallback &callback);

        void terminate();

        const std::filesystem::path &path() const;
        bool isOpen() const;

        srt::core::NO<srt::core::TaskResult> result() const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // SRT_DRIVER_ONNX_SESSION_H
