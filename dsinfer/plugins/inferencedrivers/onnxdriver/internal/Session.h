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

        /// After move \a construction the source holds no implementation. It may only be destroyed
        /// or assigned to, as every other member function requires a live \c Session.
        ///
        /// Move \a assignment swaps instead, so the source stays usable but takes over whatever
        /// session this object held before.
        Session(Session &&other) noexcept;
        Session &operator=(Session &&other) noexcept;

    public:
        srt::Expected<void> open(const std::filesystem::path &path,
                                 const Api::Onnx::SessionOpenArgs &args);
        srt::Expected<void> close();

        const std::vector<std::string> &inputNames() const;
        const std::vector<std::string> &outputNames() const;

        srt::Expected<std::unique_ptr<srt::TaskResult>> run(const srt::TaskStartInput &input);
        srt::Expected<void> runAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                     srt::ITask::AsyncCallback callback);

        void terminate();
        void waitForFinished();

        const std::filesystem::path &path() const;
        bool isOpen() const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // DSINFER_ONNXDRIVER_SESSION_H
