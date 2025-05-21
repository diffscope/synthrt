#ifndef DSINFER_ONNXDRIVER_SESSION_H
#define DSINFER_ONNXDRIVER_SESSION_H

#include <map>
#include <memory>
#include <filesystem>
#include <functional>

#include <synthrt/Support/Error.h>
#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>


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
        bool open(const std::filesystem::path &path, const srt::NO<Api::Onnx::SessionOpenArgs> &args, srt::Error *error);
        bool close();

        const std::vector<std::string> &inputNames() const;
        const std::vector<std::string> &outputNames() const;

        bool run(const srt::NO<Api::Onnx::SessionStartInput> &input, srt::NO<Api::Onnx::SessionResult> &outResult, srt::Error *error = nullptr);

        void terminate();

        std::filesystem::path path() const;
        bool isOpen() const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // DSINFER_ONNXDRIVER_SESSION_H
