#ifndef SYNTHRT_INFERENCESESSION_H
#define SYNTHRT_INFERENCESESSION_H

#include <filesystem>

#include <synthrt/Support/Error.h>
#include <synthrt/Task/AbstractTask.h>

#include <dsinfer/dsinfer_global.h>

namespace ds {

    class InferenceSessionOpenArgs : public srt::NamedObject {
    public:
        InferenceSessionOpenArgs(const std::string &name) : srt::NamedObject(name) {
        }
    };

    /// InferenceSession - Provides a basic interface for the memory image of an AI model.
    class InferenceSession : public srt::AbstractTask {
    public:
        virtual bool open(const std::filesystem::path &path,
                          const srt::NO<InferenceSessionOpenArgs> &args, srt::Error *error) = 0;
        virtual bool close(srt::Error *error) = 0;
        virtual bool isOpen() const = 0;

        virtual int64_t id() const = 0;
    };

}

#endif // SYNTHRT_INFERENCESESSION_H