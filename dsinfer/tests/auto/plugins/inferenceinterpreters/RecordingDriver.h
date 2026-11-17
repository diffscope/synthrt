#ifndef DSINFER_TESTS_PLUGINS_INFERENCEINTERPRETERS_RECORDINGDRIVER_H
#define DSINFER_TESTS_PLUGINS_INFERENCEINTERPRETERS_RECORDINGDRIVER_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h>
#include <dsinfer/Core/Tensor.h>
#include <dsinfer/Inference/InferenceDriver.h>
#include <dsinfer/Inference/InferenceSession.h>
#include <dsinfer/Support/ErrorCode.h>

namespace ds::test {

    /// Selects the response of the session whose file name is faultModel.
    enum class SessionFault {
        None,
        Open,
        Run,
        NullResult,
        WrongType,
        WrongVersion,
        MissingOutput,
        NullTensor
    };

    /// One request observed at the model boundary.
    struct ModelCall {
        std::filesystem::path model;
        std::map<std::string, std::shared_ptr<ITensor>> inputs;
        std::set<std::string> outputs;
    };

    /// Records model requests and controls failures without invoking an inference engine.
    struct DriverTrace {
        std::vector<ModelCall> calls;
        std::vector<std::filesystem::path> openedModels;
        std::atomic<int> created{0};
        std::atomic<int> destroyed{0};
        std::atomic<int> stopped{0};
        std::atomic<int> waited{0};
        SessionFault fault = SessionFault::None;
        std::string faultModel = "model.onnx";
        bool block = false;
        std::mutex mutex;
        std::condition_variable changed;
        bool entered = false;
        bool interrupted = false;

        /// Waits for the model call to enter a deliberately blocked session.
        bool waitUntilEntered() {
            std::unique_lock<std::mutex> lock(mutex);
            return changed.wait_for(lock, std::chrono::seconds(5), [this] { return entered; });
        }
    };

    /// Returns a different result contract to exercise the task's checked cast boundary.
    class UnexpectedResult : public srt::TaskResult {
    public:
        UnexpectedResult(std::string type, int version) : TaskResult(std::move(type), version) {
        }
    };

    /// A session that records the tensors supplied by a real inference task.
    class RecordingSession : public InferenceSession {
    public:
        explicit RecordingSession(std::shared_ptr<DriverTrace> trace)
            : m_trace(std::move(trace)), m_id(++m_trace->created) {
        }
        ~RecordingSession() {
            ++m_trace->destroyed;
        }

        srt::Expected<void> open(const std::filesystem::path &path,
                                 const InferenceSessionOpenArgs &args) override {
            m_path = path;
            m_trace->openedModels.push_back(path);
            if (args.type() != Api::Onnx::API_NAME || args.version() != Api::Onnx::API_VERSION) {
                return srt::Error(srt::Error::InvalidArgument, "incorrect model open contract");
            }
            if (selectedFault() == SessionFault::Open) {
                return srt::Error(srt::Error::FileNotOpen, "model open refused");
            }
            m_open = true;
            return {};
        }
        srt::Expected<void> close() override {
            m_open = false;
            return {};
        }
        bool isOpen() const override {
            return m_open;
        }
        int64_t id() const override {
            return m_id;
        }

        srt::Expected<std::unique_ptr<srt::TaskResult>>
            start(const srt::TaskStartInput &input) override {
            if (!m_open || input.type() != Api::Onnx::API_NAME ||
                input.version() != Api::Onnx::API_VERSION) {
                return srt::Error(srt::Error::InvalidArgument, "incorrect model start contract");
            }
            setState(Running);
            const auto typed = input.as<Api::Onnx::SessionStartInput>();
            m_trace->calls.push_back({m_path, typed->inputs, typed->outputs});
            if (m_trace->block) {
                std::unique_lock<std::mutex> lock(m_trace->mutex);
                m_trace->entered = true;
                m_trace->changed.notify_all();
                m_trace->changed.wait(lock, [this] { return m_trace->interrupted; });
                setState(Canceled);
                return srt::Error(ErrorCode::SessionFailed, "model interrupted");
            }
            const auto fault = selectedFault();
            if (fault == SessionFault::Run) {
                setState(Failed);
                return srt::Error(ErrorCode::SessionFailed, "model execution refused");
            }
            setState(Succeeded);
            if (fault == SessionFault::NullResult) {
                return std::unique_ptr<srt::TaskResult>();
            }
            if (fault == SessionFault::WrongType || fault == SessionFault::WrongVersion) {
                return std::make_unique<UnexpectedResult>(
                    fault == SessionFault::WrongType ? "other.backend" : Api::Onnx::API_NAME, 99);
            }
            auto result = std::make_unique<Api::Onnx::SessionResult>();
            if (fault != SessionFault::MissingOutput) {
                for (const auto &name : typed->outputs) {
                    if (fault == SessionFault::NullTensor) {
                        result->outputs.emplace(name, nullptr);
                        continue;
                    }
                    std::vector<int64_t> shape{1, 4};
                    if (name == "encoder_out") {
                        shape = {1, 1, 2};
                    } else if (name == "x_masks" || name == "ph_dur_pred") {
                        shape = {1, 1};
                    } else if (name == "mel") {
                        shape = {1, 4, 2};
                    }
                    if (name == "x_masks") {
                        auto tensor = Tensor::createFilled<bool>(shape, true);
                        if (!tensor) {
                            return tensor.takeError();
                        }
                        result->outputs.emplace(name, tensor.take());
                        continue;
                    }
                    auto tensor = Tensor::createFilled<float>(shape, 2.0f);
                    if (!tensor) {
                        return tensor.takeError();
                    }
                    result->outputs.emplace(name, tensor.take());
                }
            }
            setState(Succeeded);
            return std::unique_ptr<srt::TaskResult>(std::move(result));
        }
        srt::Expected<void> stop() override {
            ++m_trace->stopped;
            std::lock_guard<std::mutex> lock(m_trace->mutex);
            m_trace->interrupted = true;
            m_trace->changed.notify_all();
            return {};
        }
        srt::Expected<void> waitForFinished() override {
            ++m_trace->waited;
            return {};
        }

    private:
        SessionFault selectedFault() const {
            return m_path.filename() == m_trace->faultModel ? m_trace->fault : SessionFault::None;
        }

        std::shared_ptr<DriverTrace> m_trace;
        std::filesystem::path m_path;
        int64_t m_id;
        bool m_open = false;
    };

    /// Supplies recording sessions through the same service boundary as a real ONNX driver.
    class RecordingDriver : public InferenceDriver {
    public:
        explicit RecordingDriver(std::shared_ptr<DriverTrace> trace)
            : InferenceDriver(Api::Onnx::API_NAME), m_trace(std::move(trace)) {
        }
        srt::Expected<void> initialize(const InferenceDriverInitArgs &) override {
            return {};
        }
        std::unique_ptr<InferenceSession> createSession() override {
            return std::make_unique<RecordingSession>(m_trace);
        }

    private:
        std::shared_ptr<DriverTrace> m_trace;
    };
}

#endif // DSINFER_TESTS_PLUGINS_INFERENCEINTERPRETERS_RECORDINGDRIVER_H
