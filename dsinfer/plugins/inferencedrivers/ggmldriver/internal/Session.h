#ifndef DSINFER_GGMLDRIVER_SESSION_H
#define DSINFER_GGMLDRIVER_SESSION_H

#include <map>
#include <memory>
#include <filesystem>
#include <string>
#include <vector>

#include <synthrt/Support/Expected.h>
#include <synthrt/Task/ITask.h>
#include <dsinfer/Api/Drivers/Common/CommonDriverApi.h>

namespace ds::ggmldriver {

    class Session {
    public:
        Session();
        ~Session();

        Session(const Session &) = delete;
        Session &operator=(const Session &) = delete;

    public:
        srt::Expected<void> open(const std::filesystem::path &path,
                                 const srt::NO<Api::Common::SessionOpenArgs> &args);
        srt::Expected<void> close();

        bool isOpen() const;

        srt::Expected<srt::NO<srt::TaskResult>> run(const srt::NO<srt::TaskStartInput> &input);

        srt::NO<srt::TaskResult> result() const;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}

#endif // DSINFER_GGMLDRIVER_SESSION_H
