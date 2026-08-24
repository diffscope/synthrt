#ifndef DSINFER_ONNXDRIVER_SESSION_H
#define DSINFER_ONNXDRIVER_SESSION_H

#include <filesystem>
#include <memory>

#include <onnxruntime_cxx_api.h>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>

#include <synthrt/Support/Expected.h>
#include <synthrt/Task/ITask.h>

#include "SessionSystem.h"

namespace ds::onnxdriver {

    class DriverContext;
    struct SessionExecutionState;
    struct SessionRunContext;

    /// Owns one opened model reference and serializes its executions.
    class Session {
    public:
        /// Creates a closed session sharing \a context.
        explicit Session(std::shared_ptr<DriverContext> context);
        ~Session();

        Session(const Session &) = delete;
        Session &operator=(const Session &) = delete;
        Session(Session &&) = delete;
        Session &operator=(Session &&) = delete;

        /// Opens \a path and acquires its cached model image.
        srt::Expected<void> open(const std::filesystem::path &path,
                                 const Api::Onnx::SessionOpenArgs &args);

        /// Waits for execution and releases the cached model image.
        srt::Expected<void> close();

        /// Runs one inference synchronously.
        srt::Expected<std::unique_ptr<srt::TaskResult>> run(const srt::TaskStartInput &input);

        /// Starts one inference and invokes \a callback after ORT completes.
        srt::Expected<void> runAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                     srt::ITask::AsyncCallback callback);

        /// Requests termination of the active ORT run.
        void terminate();

        /// Waits for the active run and its callback to finish.
        void waitForFinished();

        /// Returns the canonical path of the opened model.
        const std::filesystem::path &path() const;

        /// Returns whether this session holds a model image.
        bool isOpen() const;

        /// Returns whether an execution or its completion callback is active.
        bool isRunning();

    private:
        struct AsyncRun;

        bool beginRun();
        void endRun();
        void waitForRun();
        srt::Error validateInput(const Api::Onnx::SessionStartInput &input) const;
        srt::Expected<void> prepareRunContext(const Api::Onnx::SessionStartInput &input,
                                              SessionRunContext &runContext) const;
        static void completeAsyncRun(std::unique_ptr<AsyncRun> run,
                                     srt::Expected<std::unique_ptr<srt::TaskResult>> result);
        static srt::Expected<std::unique_ptr<srt::TaskResult>>
            collectAsyncOutputs(AsyncRun &run, OrtValue **outputs, size_t outputCount);
        static void runAsyncCallback(void *userData, OrtValue **outputs, size_t outputCount,
                                     OrtStatusPtr status) noexcept;
        srt::Expected<std::unique_ptr<srt::TaskResult>>
            sessionRun(const Api::Onnx::SessionStartInput &sessionStartInput);
        srt::Expected<void>
            sessionRunAsync(std::shared_ptr<const Api::Onnx::SessionStartInput> sessionStartInput,
                            srt::ITask::AsyncCallback callback);

        // Declared first so the driver context outlives all objects and cache references below.
        std::shared_ptr<DriverContext> m_driverContext;
        Ort::RunOptions m_runOptions;
        SessionSystem::ImageGroup *m_group = nullptr;
        SessionImage *m_image = nullptr;
        bool m_preferCpu = false;
        std::filesystem::path m_realPath;
        std::shared_ptr<SessionExecutionState> m_executionState;
    };

}

#endif // DSINFER_ONNXDRIVER_SESSION_H
