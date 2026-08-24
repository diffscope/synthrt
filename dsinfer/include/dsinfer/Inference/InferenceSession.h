#ifndef DSINFER_INFERENCESESSION_H
#define DSINFER_INFERENCESESSION_H

#include <cstdint>
#include <filesystem>

#include <synthrt/Support/Expected.h>
#include <synthrt/Task/ITask.h>

#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/dsinfer_global.h>

namespace ds {

    /// Arguments used to open one inference model.
    class InferenceSessionOpenArgs : public InferenceDriverPayload {
    public:
        virtual ~InferenceSessionOpenArgs() = default;

    protected:
        using InferenceDriverPayload::InferenceDriverPayload;
    };

    /// Initialization data supplied before session execution.
    class InferenceSessionInitArgs : public srt::TaskInitArgs {
    public:
        virtual ~InferenceSessionInitArgs() = default;

    protected:
        using TaskInitArgs::TaskInitArgs;
    };

    /// Input supplied for one session execution.
    class InferenceSessionStartInput : public srt::TaskStartInput {
    public:
        virtual ~InferenceSessionStartInput() = default;

    protected:
        using TaskStartInput::TaskStartInput;
    };

    /// Successful output produced by one session execution.
    class InferenceSessionResult : public srt::TaskResult {
    public:
        virtual ~InferenceSessionResult() = default;

    protected:
        using TaskResult::TaskResult;
    };

    /// A reusable execution session for one inference model.
    class DSINFER_EXPORT InferenceSession : public srt::ITask {
    public:
        virtual ~InferenceSession();

        /// Opens the model at \a path using backend specific arguments.
        virtual srt::Expected<void> open(const std::filesystem::path &path,
                                         const InferenceSessionOpenArgs &args) = 0;

        /// Releases the opened model and its session resources.
        virtual srt::Expected<void> close() = 0;

        /// Returns whether this session currently holds an open model.
        virtual bool isOpen() const = 0;

        /// Returns an identifier unique within the driver runtime.
        virtual int64_t id() const = 0;

    protected:
        InferenceSession();
    };

}

#endif // DSINFER_INFERENCESESSION_H
