#ifndef DSINFER_INFERENCESESSION_H
#define DSINFER_INFERENCESESSION_H

#include <filesystem>

#include <synthrt/Support/Error.h>
#include <synthrt/Task/AbstractTask.h>

#include <dsinfer/dsinfer_global.h>

namespace ds {

    class InferenceSessionOpenArgs : public srt::NamedObject {
    public:
        inline InferenceSessionOpenArgs(const std::string &name, int version)
            : srt::NamedObject(name), version(version) {
        }

        int version;
    };

    class InferenceSessionInitArgs : public srt::TaskInitArgs {
    public:
        inline InferenceSessionInitArgs(const std::string &name, int version)
            : srt::TaskInitArgs(name), version(version) {
        }

        int version;
    };


    class InferenceSessionStartInput : public srt::TaskStartInput {
    public:
        inline InferenceSessionStartInput(const std::string &name, int version)
            : srt::TaskStartInput(name), version(version) {
        }

        int version;
    };

    class InferenceSessionResult : public srt::TaskResult {
    public:
        inline InferenceSessionResult(const std::string &name, int version)
            : srt::TaskResult(name), version(version) {
        }

        int version;
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

#endif // DSINFER_INFERENCESESSION_H