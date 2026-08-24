#include "Session.h"

#include <array>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>

#include <blake3.h>

#include <stdcorelib/adt/vlarray.h>

#include <dsinfer/Support/ErrorCode.h>

#include "OnnxTensor.h"

#include "OnnxDriverLogging.h"
#include "Runtime/DriverContext.h"
#include "SessionImage.h"
#include "SessionSystem.h"
#include "Utils/ScopedTimer.h"

namespace fs = std::filesystem;

namespace ds::onnxdriver {

    // Keeps the names, tensors, and raw ORT pointers for one execution together.
    struct SessionRunContext {
        stdc::vlarray<const char *, 8> inputNames;
        stdc::vlarray<const char *, 8> outputNames;

        // Keeps converted tensors alive while ORT borrows their values.
        stdc::vlarray<std::shared_ptr<OnnxTensor>, 8> inputTensors;

        // OrtValue pointers for ORT api use. The vector does not own the values.
        stdc::vlarray<OrtValue *, 8> inputValuePtrs;

        // Output value pointers from session run.
        // The vector does not own the values, so they need manually memory management.
        stdc::vlarray<OrtValue *, 8> outputValuePtrs;

        SessionRunContext() = default;

        explicit SessionRunContext(size_t inputSize, size_t outputSize)
            : outputValuePtrs(outputSize, nullptr) {
            inputNames.reserve(inputSize);
            outputNames.reserve(outputSize);
            inputTensors.reserve(inputSize);
            inputValuePtrs.reserve(inputSize);
        }

        // Disable copying
        SessionRunContext(const SessionRunContext &) = delete;
        SessionRunContext &operator=(const SessionRunContext &) = delete;

        ~SessionRunContext() {
            releaseOutputValues();
        }

        void releaseOutputValues() {
            for (OrtValue *&valuePtr : outputValuePtrs) {
                if (valuePtr) {
                    Ort::GetApi().ReleaseValue(valuePtr);
                    valuePtr = nullptr;
                }
            }
        }
    };

    // Separates callback lifetime from Session lifetime. A callback can destroy its public
    // OnnxSession while other threads still wait for the callback to return.
    struct SessionExecutionState {
        std::mutex mutex;
        std::condition_variable cv;
        bool running = false;
        std::thread::id callbackThread;

        bool begin() {
            std::lock_guard<std::mutex> lock(mutex);
            if (running) {
                return false;
            }
            running = true;
            callbackThread = std::thread::id();
            return true;
        }

        bool isRunning() {
            std::lock_guard<std::mutex> lock(mutex);
            return running;
        }

        void enterCallback() {
            std::lock_guard<std::mutex> lock(mutex);
            callbackThread = std::this_thread::get_id();
        }

        void end() {
            {
                std::lock_guard<std::mutex> lock(mutex);
                running = false;
                callbackThread = std::thread::id();
            }
            cv.notify_all();
        }

        void wait() {
            std::unique_lock<std::mutex> lock(mutex);
            if (callbackThread == std::this_thread::get_id()) {
                return;
            }
            cv.wait(lock, [this] { return !running; });
        }
    };

    // Owns everything ORT borrows until its asynchronous callback runs.
    struct Session::AsyncRun {
        AsyncRun(std::shared_ptr<SessionExecutionState> executionState, Ort::RunOptions &runOptions,
                 std::filesystem::path path,
                 std::shared_ptr<const Api::Onnx::SessionStartInput> input,
                 srt::ITask::AsyncCallback callback)
            : executionState(std::move(executionState)), runOptions(&runOptions),
              path(std::move(path)), input(std::move(input)), callback(std::move(callback)),
              context(this->input->inputs.size(), this->input->outputs.size()),
              startedAt(std::chrono::steady_clock::now()) {
        }

        std::shared_ptr<SessionExecutionState> executionState;
        Ort::RunOptions *runOptions;
        std::filesystem::path path;
        std::shared_ptr<const Api::Onnx::SessionStartInput> input;
        srt::ITask::AsyncCallback callback;
        SessionRunContext context;
        std::chrono::time_point<std::chrono::steady_clock> startedAt;
    };

    bool Session::beginRun() {
        return m_executionState->begin();
    }

    void Session::endRun() {
        m_executionState->end();
    }

    void Session::waitForRun() {
        m_executionState->wait();
    }

    srt::Error Session::validateInput(const Api::Onnx::SessionStartInput &input) const {
        const auto &inputValueMap = input.inputs;
        if (inputValueMap.empty()) {
            return {ds::ErrorCode::InvalidInput, "input map is empty"};
        }

        const auto &requiredInputNames = m_image->inputNames();
        std::ostringstream msgStream;
        msgStream << '[' << m_realPath.filename() << ']' << ' ';

        // Check for missing and extra input names. If found, return empty map and the error
        // message.
        {
            bool flagMissing = false;

            // Check for missing input names
            for (const auto &requiredInputName : requiredInputNames) {
                if (inputValueMap.find(requiredInputName) == inputValueMap.end()) {
                    if (flagMissing) {
                        // It isn't the first missing input name. Append a comma separator.
                        msgStream << ',' << ' ';
                    } else {
                        // It's the first missing input name. Append the message intro.
                        msgStream << "missing input names: ";
                        flagMissing = true;
                    }
                    msgStream << '"' << requiredInputName << '"';
                }
            }

            // Check for extra input names
            bool flagExtra = false;
            std::unordered_set<std::string> requiredSet(requiredInputNames.begin(),
                                                        requiredInputNames.end());
            for (auto &it : std::as_const(inputValueMap)) {
                auto &actualInputName = it.first;
                if (requiredSet.find(actualInputName) == requiredSet.end()) {
                    if (flagExtra) {
                        msgStream << ',' << ' ';
                    } else {
                        if (flagMissing) {
                            msgStream << ';' << ' ';
                        }
                        msgStream << "extra input names: ";
                        flagExtra = true;
                    }
                    msgStream << '"' << actualInputName << '"';
                }
            }

            if (flagMissing || flagExtra) {
                return {ds::ErrorCode::InvalidInput, msgStream.str()};
            }
        }
        for (const auto &[name, value] : inputValueMap) {
            if (!value) {
                return {ds::ErrorCode::InvalidInput, "input tensor must not be null: " + name};
            }
        }

        if (input.outputs.empty()) {
            return {ds::ErrorCode::InvalidInput, "output set is empty"};
        }

        const auto &outputNames = m_image->outputNames();
        const std::unordered_set<std::string> availableOutputs(outputNames.begin(),
                                                               outputNames.end());
        for (const auto &name : input.outputs) {
            if (availableOutputs.find(name) == availableOutputs.end()) {
                return {ds::ErrorCode::InvalidInput, "unknown output name: " + name};
            }
        }

        return {};
    }

    srt::Expected<void> Session::prepareRunContext(const Api::Onnx::SessionStartInput &input,
                                                   SessionRunContext &runContext) const {
        if (auto error = validateInput(input); !error.ok()) {
            return error;
        }

        for (const auto &[name, value] : input.inputs) {
            runContext.inputNames.push_back(name.c_str());

            auto onnxTensor = std::dynamic_pointer_cast<OnnxTensor>(value);
            if (!onnxTensor) {
                auto converted = OnnxTensor::createFromTensor(value);
                if (!converted) {
                    return converted.takeError().withContext("failed to convert input: " + name);
                }
                onnxTensor = converted.take();
            }
            if (!onnxTensor->isValid()) {
                return srt::Error(ds::ErrorCode::InvalidInput, "input tensor is invalid: " + name);
            }

            runContext.inputValuePtrs.push_back(onnxTensor->ortValue());
            runContext.inputTensors.push_back(std::move(onnxTensor));
        }

        for (const auto &name : input.outputs) {
            runContext.outputNames.push_back(name.c_str());
        }
        return {};
    }

    void Session::completeAsyncRun(std::unique_ptr<AsyncRun> run,
                                   srt::Expected<std::unique_ptr<srt::TaskResult>> result) {
        const auto elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - run->startedAt);
        g_log.srtInfo("Session [%1] - Finished inference in %2 seconds", run->path.filename(),
                      elapsed.count());

        auto callback = std::move(run->callback);
        run->runOptions->UnsetTerminate();
        run->executionState->enterCallback();
        try {
            callback(std::move(result));
        } catch (const std::exception &error) {
            g_log.srtCritical("Asynchronous callback failed: %1", error.what());
        } catch (...) {
            g_log.srtCritical("Asynchronous callback failed with an unknown exception");
        }
        run->executionState->end();
    }

    srt::Expected<std::unique_ptr<srt::TaskResult>>
        Session::collectAsyncOutputs(AsyncRun &run, OrtValue **outputs, size_t outputCount) {
        if (outputCount != run.context.outputNames.size()) {
            return srt::Error(ds::ErrorCode::SessionFailed,
                              "ONNX Runtime returned an unexpected output count");
        }

        auto result = std::make_unique<Api::Onnx::SessionResult>();
        for (size_t i = 0; i < outputCount; ++i) {
            // Transfer ownership of the raw OrtValue pointer to OnnxTensor.
            Ort::Value managedOrtValue(outputs[i]);
            outputs[i] = nullptr;
            run.context.outputValuePtrs[i] = nullptr;

            auto tensor = OnnxTensor::createFromOrtValue(std::move(managedOrtValue));
            if (!tensor) {
                return tensor.takeError();
            }

            result->outputs.emplace(run.context.outputNames[i], tensor.take());
        }
        return std::unique_ptr<srt::TaskResult>(std::move(result));
    }

    void Session::runAsyncCallback(void *userData, OrtValue **outputs, size_t outputCount,
                                   OrtStatusPtr status) noexcept {
        auto run = std::unique_ptr<AsyncRun>(static_cast<AsyncRun *>(userData));
        try {
            Ort::Status runStatus(status);
            if (!runStatus.IsOK()) {
                completeAsyncRun(std::move(run), srt::Error(ds::ErrorCode::SessionFailed,
                                                            runStatus.GetErrorMessage()));
                return;
            }

            auto result = collectAsyncOutputs(*run, outputs, outputCount);
            completeAsyncRun(std::move(run), std::move(result));
        } catch (const std::exception &error) {
            completeAsyncRun(std::move(run),
                             srt::Error(ds::ErrorCode::SessionFailed, error.what()));
        } catch (...) {
            completeAsyncRun(
                std::move(run),
                srt::Error(ds::ErrorCode::SessionFailed,
                           "asynchronous ONNX execution failed with an unknown exception"));
        }
    }

    srt::Expected<std::unique_ptr<srt::TaskResult>>
        Session::sessionRun(const Api::Onnx::SessionStartInput &sessionStartInput) {
        if (!beginRun()) {
            return srt::Error(ds::ErrorCode::SessionFailed, "the ONNX session is already running");
        }
        struct RunGuard {
            Ort::RunOptions &runOptions;
            std::shared_ptr<SessionExecutionState> executionState;
            ~RunGuard() {
                runOptions.UnsetTerminate();
                executionState->end();
            }
        } runGuard{m_runOptions, m_executionState};

        const auto &inputValueMap = sessionStartInput.inputs;
        const auto inputCount = inputValueMap.size();
        const auto outputCount = sessionStartInput.outputs.size();
        SessionRunContext context(inputCount, outputCount);
        if (auto prepared = prepareRunContext(sessionStartInput, context); !prepared) {
            return prepared.takeError();
        }

        const auto &filename = m_realPath.filename();
        g_log.srtInfo("Session [%1] - Running inference", filename);
        ScopedTimer timer([&](ScopedTimer::Duration elapsed) {
            g_log.srtInfo("Session [%1] - Finished inference in %2 seconds", filename,
                          elapsed.count());
        });

        auto result = std::make_unique<Api::Onnx::SessionResult>();
        try {
            Ort::Status statusRun(Ort::GetApi().Run(
                m_image->session(), m_runOptions, context.inputNames.data(),
                context.inputValuePtrs.data(), inputCount, context.outputNames.data(), outputCount,
                context.outputValuePtrs.data()));

            if (!statusRun.IsOK()) {
                context.releaseOutputValues();
                return srt::Error(ds::ErrorCode::SessionFailed, statusRun.GetErrorMessage());
            }

            for (size_t i = 0; i < context.outputValuePtrs.size(); ++i) {
                // Transfer ownership of the raw OrtValue* to an Ort::Value wrapper,
                // which will subsequently be managed by OnnxTensor. No manual release is
                // required.
                Ort::Value managedOrtValue(context.outputValuePtrs[i]);

                // Null the raw pointer to prevent double release in SessionRunContext's
                // destructor.
                context.outputValuePtrs[i] = nullptr;

                auto tensorResult = OnnxTensor::createFromOrtValue(std::move(managedOrtValue));
                if (!tensorResult) {
                    return tensorResult.takeError();
                }
                result->outputs.emplace(context.outputNames[i], tensorResult.take());
            }
            return std::unique_ptr<srt::TaskResult>(std::move(result));
        } catch (const Ort::Exception &err) {
            timer.deactivate();
            return srt::Error(ds::ErrorCode::SessionFailed, err.what());
        }
    }

    srt::Expected<void> Session::sessionRunAsync(
        std::shared_ptr<const Api::Onnx::SessionStartInput> sessionStartInput,
        srt::ITask::AsyncCallback callback) {
        if (!callback) {
            return srt::Error(srt::Error::InvalidArgument,
                              "asynchronous callback must not be empty");
        }
        if (!beginRun()) {
            return srt::Error(ds::ErrorCode::SessionFailed, "the ONNX session is already running");
        }

        auto run = std::make_unique<AsyncRun>(m_executionState, m_runOptions, m_realPath,
                                              std::move(sessionStartInput), std::move(callback));
        auto &context = run->context;
        const auto inputCount = run->input->inputs.size();
        const auto outputCount = run->input->outputs.size();
        if (auto prepared = prepareRunContext(*run->input, context); !prepared) {
            m_runOptions.UnsetTerminate();
            endRun();
            return prepared.takeError();
        }

        g_log.srtInfo("Session [%1] - Running inference", m_realPath.filename());

        try {
            // ORT may invoke the callback before RunAsync returns. Transfer ownership before
            // entering ORT so only the callback can destroy a successful invocation.
            auto *runPointer = run.release();
            Ort::Status statusRun(Ort::GetApi().RunAsync(
                m_image->session(), m_runOptions, context.inputNames.data(),
                context.inputValuePtrs.data(), inputCount, context.outputNames.data(), outputCount,
                context.outputValuePtrs.data(), runAsyncCallback, runPointer));
            if (!statusRun.IsOK()) {
                run.reset(runPointer);
                m_runOptions.UnsetTerminate();
                endRun();
                context.releaseOutputValues();
                return srt::Error(ds::ErrorCode::SessionFailed, statusRun.GetErrorMessage());
            }
            return {};
        } catch (const Ort::Exception &err) {
            m_runOptions.UnsetTerminate();
            endRun();
            return srt::Error(ds::ErrorCode::SessionFailed, err.what());
        }
    }

    Session::Session(std::shared_ptr<DriverContext> context)
        : m_driverContext(std::move(context)),
          m_executionState(std::make_shared<SessionExecutionState>()) {
    }

    Session::~Session() {
        // Nothing useful can be done with a close failure while unwinding.
        static_cast<void>(close());
    }

    struct ModelFileInfo {
        std::streamsize size = 0;
        std::array<uint8_t, 32> hash{};
        std::string hashString;
    };

    static srt::Expected<ModelFileInfo> readModelFileInfo(const fs::path &path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return srt::Error(srt::Error::FileNotOpen, "failed to open ONNX model");
        }

        file.seekg(0, std::ios::end);
        ModelFileInfo result;
        result.size = file.tellg();
        if (result.size < 0) {
            return srt::Error(srt::Error::FileNotOpen, "failed to read ONNX model size");
        }
        file.seekg(0, std::ios::beg);

        std::array<char, 4096> buffer;

        blake3_hasher hasher;
        blake3_hasher_init(&hasher);

        while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
            blake3_hasher_update(&hasher, buffer.data(), file.gcount());
        }

        constexpr size_t hashByteSize = 32;
        blake3_hasher_finalize(&hasher, result.hash.data(), result.hash.size());

        static constexpr char hexDigits[] = "0123456789abcdef";
        result.hashString.resize(hashByteSize * 2);
        for (size_t i = 0; i < hashByteSize; ++i) {
            result.hashString[2 * i] = hexDigits[result.hash[i] >> 4];
            result.hashString[2 * i + 1] = hexDigits[result.hash[i] & 0x0f];
        }

        return result;
    }

    srt::Expected<void> Session::open(const fs::path &path,
                                      const Api::Onnx::SessionOpenArgs &args) {
        if (isOpen()) {
            g_log.srtWarning("Session - Session %1 is already open!", path.string());
            return srt::Error(ds::ErrorCode::AlreadyOpen, "session is already open");
        }

        // Open
        g_log.srtDebug("Session - Try open " + path.string());
        if (!fs::is_regular_file(path)) {
            return srt::Error(srt::Error::FileNotOpen, "not a regular file");
        }

        std::error_code pathError;
        const auto canonicalPath = fs::canonical(path, pathError);
        if (pathError) {
            return srt::Error(srt::Error::FileNotOpen,
                              "failed to resolve model path: " + pathError.message());
        }
        g_log.srtDebug("Session - The canonical path is " + canonicalPath.string());

        auto &sessionSystem = m_driverContext->sessionSystem();
        // Model creation is serialized per driver, but the cache lock is released while ORT reads
        // and initializes the model. Existing sessions can still close during that work.
        std::lock_guard<std::mutex> openLock(sessionSystem.openMutex);
        SessionImage *image = nullptr;
        std::array<uint8_t, 32> hash{};
        std::streamsize size = 0;

        const bool preferCpu = args.useCpu;
        SessionSystem::ImageGroup *imageGroup = nullptr;

        const auto acquireImage = [&](SessionSystem::ImageGroup &group) {
            const auto imageIt = group.images.find(preferCpu);
            if (imageIt == group.images.end()) {
                return static_cast<SessionImage *>(nullptr);
            }
            auto &data = imageIt->second;
            ++data.referenceCount;
            g_log.srtDebug(
                "Session - The session image already exists. Increasing the reference count");
            return data.image.get();
        };

        {
            std::unique_lock<std::shared_mutex> lock(sessionSystem.mutex);
            if (auto pathIt = sessionSystem.pathMap.find(canonicalPath);
                pathIt != sessionSystem.pathMap.end()) {
                imageGroup = &*pathIt->second;
                hash = imageGroup->hash;
                size = imageGroup->size;
                image = acquireImage(*imageGroup);
            }
        }

        if (!imageGroup) {
            auto fileInfo = readModelFileInfo(canonicalPath);
            if (!fileInfo) {
                return fileInfo.takeError();
            }
            auto info = fileInfo.take();
            size = info.size;
            hash = std::move(info.hash);
            g_log.srtDebug("Session - BLAKE3 hash is %1", info.hashString);

            std::unique_lock<std::shared_mutex> lock(sessionSystem.mutex);
            if (auto hashIt = sessionSystem.hashSizeMap.find({size, hash});
                hashIt != sessionSystem.hashSizeMap.end()) {
                imageGroup = &*hashIt->second;
                image = acquireImage(*imageGroup);
            }
        }

        if (image) {
            m_group = imageGroup;
            m_image = image;
            m_preferCpu = preferCpu;
            m_realPath = canonicalPath;
            return {};
        }

        g_log.srtDebug("Session - Creating a session image");
        auto newImage = std::make_unique<SessionImage>();
        if (auto opened = newImage->open(*m_driverContext, canonicalPath, preferCpu); !opened) {
            return opened.takeError().withContext("failed to open ONNX model");
        }

        std::unique_lock<std::shared_mutex> lock(sessionSystem.mutex);

        // A close operation can remove an empty group while ORT is creating the model. Locate the
        // group again before publishing the new image.
        imageGroup = nullptr;
        if (auto pathIt = sessionSystem.pathMap.find(canonicalPath);
            pathIt != sessionSystem.pathMap.end()) {
            imageGroup = &*pathIt->second;
        } else if (auto hashIt = sessionSystem.hashSizeMap.find({size, hash});
                   hashIt != sessionSystem.hashSizeMap.end()) {
            imageGroup = &*hashIt->second;
        }

        if (!imageGroup) {
            SessionSystem::ImageGroup group;
            group.path = canonicalPath;
            group.size = size;
            group.hash = std::move(hash);

            const auto groupIt =
                sessionSystem.imageList.emplace(sessionSystem.imageList.end(), std::move(group));
            sessionSystem.pathMap[groupIt->path] = groupIt;
            sessionSystem.hashSizeMap[{size, groupIt->hash}] = groupIt;
            imageGroup = &*groupIt;
        }

        image = newImage.get();
        imageGroup->images[preferCpu] = {std::move(newImage), 1};
        m_group = imageGroup;
        m_image = image;
        m_preferCpu = preferCpu;
        m_realPath = canonicalPath;
        return {};
    }

    srt::Expected<void> Session::close() {
        // An async run may still be reading Session state from an ORT worker thread. Waiting keeps
        // the model image and run options alive until that access and the callback have finished.
        waitForRun();

        if (!m_group) {
            return srt::Error(ds::ErrorCode::NotInitialized, "session is not open");
        }

        const auto &path = m_realPath;
        const auto &filename = path.filename();
        g_log.srtDebug("Session [%1] - close", filename);

        auto &sessionSystem = m_driverContext->sessionSystem();
        std::unique_lock<std::shared_mutex> lock(sessionSystem.mutex);

        auto &group = *m_group;
        auto &images = group.images;
        const auto imageIt = images.find(m_preferCpu);
        assert(imageIt != images.end());
        auto &data = imageIt->second;
        assert(data.referenceCount > 0);
        --data.referenceCount;
        if (data.referenceCount == 0) {
            g_log.srtDebug("SessionImage [%1] - delete", filename);
            images.erase(imageIt);
        } else {
            g_log.srtDebug("SessionImage [%1] - ref(), now ref count = %2", filename,
                           data.referenceCount);
        }

        if (images.empty()) {
            g_log.srtDebug("Session - The session image group is empty. Destroying.");
            const auto hashIt = sessionSystem.hashSizeMap.find({group.size, group.hash});
            assert(hashIt != sessionSystem.hashSizeMap.end());

            const auto listIt = hashIt->second;

            sessionSystem.hashSizeMap.erase(hashIt);
            sessionSystem.pathMap.erase(group.path);
            sessionSystem.imageList.erase(listIt);
        }

        m_group = nullptr;
        m_image = nullptr;
        m_preferCpu = false;
        m_realPath.clear();
        return {};
    }

    const std::filesystem::path &Session::path() const {
        return m_realPath;
    }

    bool Session::isOpen() const {
        return m_group != nullptr;
    }

    bool Session::isRunning() {
        return m_executionState->isRunning();
    }

    void Session::terminate() {
        m_runOptions.SetTerminate();
    }

    srt::Expected<std::unique_ptr<srt::TaskResult>> Session::run(const srt::TaskStartInput &input) {
        if (input.type() != Api::Onnx::API_NAME || input.version() != Api::Onnx::API_VERSION) {
            return srt::Error(srt::Error::InvalidArgument, "invalid task start input");
        }
        if (!m_group) {
            return srt::Error(ds::ErrorCode::NotInitialized, "session is not open");
        }
        return sessionRun(*input.as<Api::Onnx::SessionStartInput>());
    }

    srt::Expected<void> Session::runAsync(std::shared_ptr<const srt::TaskStartInput> input,
                                          srt::ITask::AsyncCallback callback) {
        if (!input || input->type() != Api::Onnx::API_NAME ||
            input->version() != Api::Onnx::API_VERSION) {
            return srt::Error(srt::Error::InvalidArgument, "invalid task start input");
        }
        if (!m_group) {
            return srt::Error(ds::ErrorCode::NotInitialized, "session is not open");
        }
        return sessionRunAsync(
            std::static_pointer_cast<const Api::Onnx::SessionStartInput>(std::move(input)),
            std::move(callback));
    }

    void Session::waitForFinished() {
        waitForRun();
    }

}
