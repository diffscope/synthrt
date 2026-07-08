#ifndef SRT_DRIVER_INFERENCESESSION_H
#define SRT_DRIVER_INFERENCESESSION_H

#include <cstdint>
#include <filesystem>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Task/ITask.h>

#include <synthrt/Driver/srt_driver_global.h>

namespace srt::driver {

    class SRT_DRIVER_EXPORT InferenceSessionOpenArgs : public srt::core::NamedObject {
    public:
        inline InferenceSessionOpenArgs(std::string name, int version)
            : srt::core::NamedObject(std::move(name)), version(version) {
        }

        int version;
    };

    class SRT_DRIVER_EXPORT InferenceSessionInitArgs : public srt::core::TaskInitArgs {
    public:
        inline InferenceSessionInitArgs(std::string name, int version)
            : srt::core::TaskInitArgs(std::move(name)), version(version) {
        }

        int version;
    };

    class SRT_DRIVER_EXPORT InferenceSessionStartInput : public srt::core::TaskStartInput {
    public:
        inline InferenceSessionStartInput(std::string name, int version)
            : srt::core::TaskStartInput(std::move(name)), version(version) {
        }

        int version;
    };

    class SRT_DRIVER_EXPORT InferenceSessionResult : public srt::core::TaskResult {
    public:
        InferenceSessionResult(std::string name, int version);
        ~InferenceSessionResult() override;

        int version;
    };

    /// InferenceSession - Provides a basic interface for the memory image of an AI model.
    class SRT_DRIVER_EXPORT InferenceSession : public srt::core::ITask {
    public:
        InferenceSession();
        virtual ~InferenceSession();

    public:
        virtual srt::core::Expected<void> open(const std::filesystem::path &path,
                                               const srt::core::NO<InferenceSessionOpenArgs> &args) = 0;
        virtual srt::core::Expected<void> close() = 0;
        virtual bool isOpen() const = 0;

        virtual int64_t id() const = 0;
    };

}

#endif // SRT_DRIVER_INFERENCESESSION_H
