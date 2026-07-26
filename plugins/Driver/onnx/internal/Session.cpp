#include "Session.h"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <fstream>
#include <unordered_set>
#include <algorithm>
#include <list>
#include <numeric>
#include <iomanip>
#include <utility>
#include <limits>

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>

#include <synthrt/Core/srt_core_global.h>
#include <synthrt/Core/Support/Expected.h>

#include <blake3.h>

#include "OnnxDriver_Logger.h"
#include "SessionImage.h"
#include "ScopedTimer.h"
#include "Env.h"

#include <synthrt/Driver/onnx/OnnxTensor.h>


namespace fs = std::filesystem;

namespace srt::driver::onnx {

    struct SessionSystem {
        struct ImageData {
            SessionImage *image;
            int count;
        };

        struct ImageGroup {
            std::filesystem::path path;
            std::streamsize size = 0;
            std::vector<uint8_t> hash;
            std::map<int, ImageData> images; // hint -> [ image, count ]
        };

        struct HashSizeKey {
            std::streamsize size;
            std::vector<uint8_t> hash;

            bool operator<(const HashSizeKey &other) const {
                if (size == other.size) {
                    return std::lexicographical_compare(hash.begin(), hash.end(),
                                                        other.hash.begin(), other.hash.end());
                }
                return size < other.size;
            }
        };

        std::list<ImageGroup> image_list;

        using ListIterator = decltype(image_list)::iterator;

        std::map<std::filesystem::path::string_type, ListIterator> path_map;
        std::map<HashSizeKey, ListIterator> hash_size_map;

        std::shared_mutex mtx;

        static SessionSystem &global() {
            static SessionSystem instance;
            return instance;
        }
    };

    struct SessionRunContext {
        std::vector<const char *> inputNames;
        std::vector<const char *> outputNames;

        // Stores constructed Ort::Value objects from generic tensors.
        // Each value will be automatically cleaned up.
        std::vector<Ort::Value> inputValueRegistry;

        // OrtValue pointers for ORT api use. The vector does not own the values.
        std::vector<OrtValue *> inputValuePtrs;

        // Output value pointers from session run.
        // The vector does not own the values, so they need manually memory management.
        std::vector<OrtValue *> outputValuePtrs;

        SessionRunContext() = default;

        explicit SessionRunContext(size_t inputSize, size_t outputSize)
            : outputValuePtrs(outputSize, nullptr) {
            inputNames.reserve(inputSize);
            outputNames.reserve(outputSize);
            inputValueRegistry.reserve(inputSize);
            inputValuePtrs.reserve(inputSize);
        }

        // Disable copying
        SessionRunContext(const SessionRunContext &) = delete;
        SessionRunContext &operator=(const SessionRunContext &) = delete;

        ~SessionRunContext() {
            releaseOutputValues();
        }

        void initialize(size_t inputSize, size_t outputSize) {
            inputNames.clear();
            inputNames.reserve(inputSize);

            outputNames.clear();
            outputNames.reserve(outputSize);

            inputValueRegistry.clear();
            inputValueRegistry.reserve(inputSize);

            inputValuePtrs.clear();
            inputValuePtrs.reserve(inputSize);

            releaseOutputValues();
            outputValuePtrs.resize(outputSize, nullptr);
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

    struct SessionAsyncRunContext {
        srt::core::ITask::StartAsyncCallback callback;
    };

    class Session::Impl {
    public:
        std::unique_ptr<Ort::RunOptions> runOptions;

        SessionSystem::ImageGroup *group = nullptr;
        SessionImage *image = nullptr;
        int hints = 0;

        std::filesystem::path realPath;

        std::unique_ptr<SessionRunContext> context;
        std::unique_ptr<SessionAsyncRunContext> asyncContext;
        srt::core::NO<SessionResult> sessionResult;

        // BUG-DRIVER-02 / BUG-DRIVER-07: Protects sessionResult and runOptions
        // against concurrent access. result() takes a shared_lock; run(),
        // runAsync(), runAsyncCallback(), sessionRun(), sessionRunAsync() and
        // terminate() synchronize through unique_lock / shared_lock as
        // appropriate. Reuses a single shared_mutex to avoid introducing new
        // concurrency primitives beyond what Session.cpp already uses.
        mutable std::shared_mutex m_stateMutex;

        Impl() : sessionResult(srt::core::NO<SessionResult>::create()) {
        }

        static inline size_t getTensorDataTypeSize(srt::core::ITensor::DataType type) {
            switch (type) {
                case srt::core::ITensor::Float:
                    return sizeof(float);
                case srt::core::ITensor::Int64:
                    return sizeof(int64_t);
                case srt::core::ITensor::Bool:
                    return sizeof(bool);
                default:
                    return 0; // error
            }
        }

        template <typename T>
        static inline srt::core::ITensor::DataType getTensorDataType() {
            if constexpr (std::is_same_v<T, float>) {
                return srt::core::ITensor::Float;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return srt::core::ITensor::Int64;
            } else if constexpr (std::is_same_v<T, bool>) {
                return srt::core::ITensor::Bool;
            } else {
                static_assert(sizeof(T) == 0, "Unsupported type for getTensorDType");
                return srt::core::ITensor::Float; // fallback to avoid warnings, won't compile anyway due to
                                       // static_assert
            }
        }

        static inline ONNXTensorElementDataType
            toOrtTensorType(srt::core::ITensor::DataType dtype) {
            switch (dtype) {
                case srt::core::ITensor::Float: return ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
                case srt::core::ITensor::Int64: return ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64;
                case srt::core::ITensor::Bool:  return ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL;
                default: return ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
            }
        }

        static inline Ort::Value
            createOrtValueFromTensor(const srt::core::NO<srt::core::ITensor> &tensor,
                                     srt::core::Error *error = nullptr) {
            const auto &rawBuffer = tensor->rawData();
            const auto dtype = tensor->dataType();
            auto shape = tensor->shape();
            auto dataLength = tensor->elementCount();
            auto dataLengthFromShape =
                std::accumulate(shape.begin(), shape.end(), int64_t{1}, std::multiplies<>());
            if (dataLength != dataLengthFromShape) {
                if (error) {
                    *error = {srt::core::Error::InvalidArgument, "Shape does not match data length"};
                }
                return Ort::Value(nullptr);
            }

            auto onnxType = toOrtTensorType(dtype);
            if (onnxType == ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED) {
                if (error) {
                    *error = {srt::core::Error::InvalidArgument, "Unsupported data type"};
                }
                return Ort::Value(nullptr);
            }

            // Use the default ORT allocator to create an owning tensor, then copy data.
            // This is safer than wrapping external memory (avoids alignment/lifetime issues
            // with ORT internals during Run).
            Ort::AllocatorWithDefaultOptions allocator{};
            Ort::Value ortValue = Ort::Value::CreateTensor(allocator, shape.data(), shape.size(), onnxType);
            if (!ortValue) {
                if (error) {
                    *error = {srt::core::Error::InvalidArgument, "Failed to allocate ORT tensor"};
                }
                return Ort::Value(nullptr);
            }

            auto elementSize = getTensorDataTypeSize(dtype);
            auto dest = static_cast<std::byte *>(ortValue.GetTensorMutableRawData());
            if (!dest) {
                if (error) {
                    *error = {srt::core::Error::InvalidArgument, "ORT tensor has null data pointer"};
                }
                return Ort::Value(nullptr);
            }
            const size_t byteCount = dataLength * elementSize;
            // BUG-DRIVER-05: Validate that the source tensor's raw buffer is
            // large enough for the requested copy. ITensor::rawData() returns a
            // bare pointer, so use byteSize() to obtain the backing buffer size
            // and refuse to read past the end of it.
            if (tensor->byteSize() < byteCount) {
                if (error) {
                    *error = {srt::core::Error::InvalidArgument,
                              "Tensor raw buffer size is smaller than required"};
                }
                return Ort::Value(nullptr);
            }
            for (size_t i = 0; i < byteCount; ++i) {
                dest[i] = rawBuffer[i];
            }
            return ortValue;
        }

        static inline srt::core::NO<srt::core::ITensor>
            createTensorFromOrtValue(const Ort::Value &ortValue, srt::core::Error *error = nullptr) {
            if (!ortValue.IsTensor()) {
                if (error) {
                    *error = {srt::core::Error::InvalidArgument, "Ort::Value is not a tensor"};
                }
                return {};
            }

            auto typeInfo = ortValue.GetTensorTypeAndShapeInfo();
            auto shape = typeInfo.GetShape();
            auto elemType = typeInfo.GetElementType();
            auto totalSize = typeInfo.GetElementCount();

            srt::core::ITensor::DataType tensorType;
            size_t elementSize;

            switch (elemType) {
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
                    tensorType = srt::core::ITensor::Float;
                    elementSize = sizeof(float);
                    break;
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
                    tensorType = srt::core::ITensor::Int64;
                    elementSize = sizeof(int64_t);
                    break;
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:
                    tensorType = srt::core::ITensor::Bool;
                    elementSize = sizeof(bool);
                    break;
                default:
                    if (error) {
                        *error = {srt::core::Error::InvalidArgument,
                                  "Unsupported ONNX tensor element type"};
                    }
                    return {};
            }

            auto rawData =
                static_cast<const std::byte *>(ortValue.GetTensorData<void>());
            // BUG-DRIVER-06: Guard against multiplication overflow when
            // computing the byte length of the ORT tensor's data. Without this
            // check a malformed shape could wrap around and produce a short
            // array_view, leading to out-of-bounds reads downstream.
            if (elementSize > 0 &&
                totalSize > (std::numeric_limits<size_t>::max)() / elementSize) {
                if (error) {
                    *error = {srt::core::Error::InvalidArgument,
                              "ONNX tensor element count * element size overflows size_t"};
                }
                return {};
            }
            stdc::array_view<std::byte> data{rawData, rawData + totalSize * elementSize};

            if (auto exp = srt::core::Tensor::createFromRawView(tensorType, shape, data); exp) {
                return exp.take();
            } else {
                if (error) {
                    *error = exp.takeError();
                }
                return {};
            }
        }

        inline srt::core::Error
            validateInputValueMap(const srt::core::NO<SessionStartInput> &input) {
            const auto &inputValueMap = input->inputs;
            if (inputValueMap.empty()) {
                return {srt::core::Error::SessionError, "Input map is empty"};
            }

            const auto &requiredInputNames = image->inputNames;
            std::ostringstream msgStream;
            msgStream << '[' << realPath.filename() << ']' << ' ';

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
                            msgStream << "Missing input name(s): ";
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
                            msgStream << "Extra input names(s): ";
                            flagExtra = true;
                        }
                        msgStream << '"' << actualInputName << '"';
                    }
                }

                if (flagMissing || flagExtra) {
                    return {srt::core::Error::SessionError, msgStream.str()};
                }
            }
            return {}; // no error
        }

        static void runAsyncCallback(void *user_data, OrtValue **outputs, size_t num_outputs,
                                     OrtStatusPtr status) {
            auto &impl = *static_cast<Impl *>(user_data);
            // BUG-DRIVER-01: Decrement inFlightCount on every exit path so
            // that close() can detect when the async run is no longer using
            // the image. We reference impl.image (not a captured `this`)
            // because close() defers deletion of the image while
            // inFlightCount > 0, keeping both impl and impl.image alive for
            // the duration of this callback.
            struct InFlightRelease {
                SessionImage *img;
                ~InFlightRelease() { if (img) --img->inFlightCount; }
            } inflightRelease{impl.image};
            // BUG-DRIVER-02: Serialize sessionResult writes against concurrent
            // result()/run()/runAsync() calls. Declared after inflightRelease
            // so the lock is released before inFlightCount is decremented.
            std::unique_lock<std::shared_mutex> lock(impl.m_stateMutex);
            auto &ctx = *impl.context;
            impl.sessionResult->outputs.clear();
            Ort::Status runStatus(status);
            if (!runStatus.IsOK()) {
                impl.sessionResult->error = {srt::core::Error::SessionError,
                                             runStatus.GetErrorMessage()};
                impl.asyncContext->callback(impl.sessionResult, impl.sessionResult->error);
                return;
            }
            for (size_t i = 0; i < num_outputs; ++i) {
                // Transfer ownership of the raw OrtValue* to an Ort::Value wrapper,
                // which will subsequently be managed by OnnxTensor. No manual release is required.
                Ort::Value managedOrtValue(outputs[i]);

                // Null the raw pointer to prevent double release in SessionRunContext's destructor.
                outputs[i] = nullptr;

                auto exp = OnnxTensor::createFromOrtValue(std::move(managedOrtValue));
                if (!exp) {
                    impl.sessionResult->error = exp.takeError();
                    impl.asyncContext->callback(impl.sessionResult, impl.sessionResult->error);
                    return;
                }

                impl.sessionResult->outputs.emplace(
                    ctx.outputNames[i],
                    exp.take());
            }
            impl.asyncContext->callback(impl.sessionResult, impl.sessionResult->error);
        }

        inline srt::core::NO<SessionResult>
            sessionRun(const srt::core::NO<SessionStartInput> &sessionStartInput,
                       srt::core::Error *error = nullptr) {
            const auto &filename = realPath.filename();
            Log.srtInfo("Session [%1] - Running inference", filename);

            ScopedTimer timer([&](const ScopedTimer::duration_t &elapsed) {
                // When finished, print time elapsed
                auto elapsedStr = static_cast<const std::ostringstream &>(
                                      std::ostringstream()
                                      << std::fixed << std::setprecision(3) << elapsed.count())
                                      .str();
                Log.srtInfo("Session [%1] - Finished inference in %2 seconds", filename,
                            elapsedStr);
            });

            if (!(sessionStartInput && sessionStartInput->objectName() == API_NAME)) {
                if (error) {
                    *error = {srt::core::Error::InvalidArgument,
                              "Session start input is not valid"};
                }
                return {};
            }

            if (auto validateError = validateInputValueMap(sessionStartInput);
                !validateError.ok()) {
                if (error) {
                    *error = std::move(validateError);
                }
                timer.deactivate();
                return {};
            }

            const auto &inputValueMap = sessionStartInput->inputs;
            auto inputCount = inputValueMap.size();
            auto outputCount = sessionStartInput->outputs.size();

            context = std::make_unique<SessionRunContext>(inputCount, outputCount);
            auto &ctx = *context;

            auto result = srt::core::NO<SessionResult>::create();
            try {
                for (auto &[name, value] : inputValueMap) {
                    ctx.inputNames.push_back(name.c_str());
                    if (value->backend() == "tensor") {
                        auto ortValue = createOrtValueFromTensor(value, error);
                        if (!ortValue) {
                            if (error) {
                                *error = {srt::core::Error::InvalidArgument,
                                          "Could not create Ort Tensor for input name \"" + name +
                                              "\""};
                            }
                            return {};
                        }
                        ctx.inputValueRegistry.push_back(std::move(ortValue));
                        ctx.inputValuePtrs.push_back(ctx.inputValueRegistry.back());
                    } else if (value->backend() == "onnx") {
                        auto ortValue = value.as<OnnxTensor>();
                        ctx.inputValuePtrs.push_back(*(ortValue->valuePtr()));
                    } else {
                        if (error) {
                            *error = {srt::core::Error::InvalidArgument,
                                      "Unknown tensor backend for input name \"" + name + "\""};
                        }
                        return {};
                    }
                }

                for (auto &name : sessionStartInput->outputs) {
                    ctx.outputNames.push_back(name.c_str());
                }
                // BUG-DRIVER-07: Protect runOptions creation/init against
                // concurrent terminate(). The lock is released before Run()
                // so that terminate() can still interrupt an in-flight run
                // (ORT's RunOptions::SetTerminate is designed to be called
                // concurrently with Run). runOptions stays valid because it
                // is owned by Impl and only replaced under this lock.
                {
                    std::unique_lock<std::shared_mutex> optLock(m_stateMutex);
                    if (!runOptions) {
                        runOptions = std::make_unique<Ort::RunOptions>();
                    }
                    runOptions->UnsetTerminate();
                }

                Ort::Status statusRun(Ort::GetApi().Run(
                    image->session, *runOptions, ctx.inputNames.data(), ctx.inputValuePtrs.data(),
                    inputCount, ctx.outputNames.data(), outputCount, ctx.outputValuePtrs.data()));

                if (!statusRun.IsOK()) {
                    ctx.releaseOutputValues();
                    if (error) {
                        *error = srt::core::Error(srt::core::Error::SessionError,
                                                  statusRun.GetErrorMessage());
                    }
                    return {};
                }

                for (size_t i = 0; i < ctx.outputValuePtrs.size(); ++i) {
                    // Transfer ownership of the raw OrtValue* to an Ort::Value wrapper,
                    // which will subsequently be managed by OnnxTensor. No manual release is
                    // required.
                    Ort::Value managedOrtValue(ctx.outputValuePtrs[i]);

                    // Null the raw pointer to prevent double release in SessionRunContext's
                    // destructor.
                    ctx.outputValuePtrs[i] = nullptr;

                    auto exp = OnnxTensor::createFromOrtValue(std::move(managedOrtValue));
                    if (!exp) {
                        if (error) {
                            *error = exp.takeError();
                        }
                        return {};
                    }

                    result->outputs.emplace(
                        ctx.outputNames[i],
                        exp.take());
                }
                // BUG-DRIVER-02: Publish the completed result under the lock so
                // concurrent result() readers observe a consistent pointer.
                {
                    std::unique_lock<std::shared_mutex> resLock(m_stateMutex);
                    sessionResult = result;
                }
                return result;
            } catch (const Ort::Exception &err) {
                if (error) {
                    *error = srt::core::Error(srt::core::Error::SessionError, err.what());
                }
            } catch (const std::exception &err) {
                if (error) {
                    *error = srt::core::Error(srt::core::Error::SessionError,
                                              std::string("ONNX session run failed: ") + err.what());
                }
            }
            timer.deactivate();
            return {};
        }

        inline bool
            sessionRunAsync(const srt::core::NO<SessionStartInput> &sessionStartInput,
                            const srt::core::ITask::StartAsyncCallback &callback,
                            srt::core::Error *error = nullptr) {
            const auto &filename = realPath.filename();
            Log.srtInfo("Session [%1] - Running inference", filename);

            ScopedTimer timer([&](const ScopedTimer::duration_t &elapsed) {
                // When finished, print time elapsed
                auto elapsedStr = static_cast<const std::ostringstream &>(
                                      std::ostringstream()
                                      << std::fixed << std::setprecision(3) << elapsed.count())
                                      .str();
                Log.srtInfo("Session [%1] - Finished inference in %2 seconds", filename,
                            elapsedStr);
            });

            if (!(sessionStartInput && sessionStartInput->objectName() == API_NAME)) {
                if (error) {
                    *error = {srt::core::Error::InvalidArgument,
                              "Session start input is not valid"};
                }
                return false;
            }

            if (auto validateError = validateInputValueMap(sessionStartInput);
                !validateError.ok()) {
                if (error) {
                    *error = std::move(validateError);
                }
                timer.deactivate();
                return false;
            }

            const auto &inputValueMap = sessionStartInput->inputs;
            auto inputCount = inputValueMap.size();
            auto outputCount = sessionStartInput->outputs.size();

            context = std::make_unique<SessionRunContext>(inputCount, outputCount);
            auto &ctx = *context;

            asyncContext = std::make_unique<SessionAsyncRunContext>();
            try {
                for (auto &[name, value] : inputValueMap) {
                    ctx.inputNames.push_back(name.c_str());
                    if (value->backend() == "tensor") {
                        auto ortValue = createOrtValueFromTensor(value, error);
                        if (!ortValue) {
                            if (error) {
                                *error = {srt::core::Error::InvalidArgument,
                                          "Could not create Ort Tensor for input name \"" + name +
                                              "\""};
                            }
                            return false;
                        }
                        ctx.inputValueRegistry.push_back(std::move(ortValue));
                        ctx.inputValuePtrs.push_back(ctx.inputValueRegistry.back());
                    } else if (value->backend() == "onnx") {
                        auto ortValue = value.as<OnnxTensor>();
                        ctx.inputValuePtrs.push_back(*(ortValue->valuePtr()));
                    } else {
                        if (error) {
                            *error = {srt::core::Error::InvalidArgument,
                                      "Unknown tensor backend for input name \"" + name + "\""};
                        }
                        return false;
                    }
                }

                for (auto &name : sessionStartInput->outputs) {
                    ctx.outputNames.push_back(name.c_str());
                }
                // BUG-DRIVER-07: Protect runOptions creation/init against
                // concurrent terminate(), mirroring sessionRun(). Released
                // before RunAsync() so terminate() can still interrupt.
                {
                    std::unique_lock<std::shared_mutex> optLock(m_stateMutex);
                    if (!runOptions) {
                        runOptions = std::make_unique<Ort::RunOptions>();
                    }
                    runOptions->UnsetTerminate();
                }

                asyncContext->callback = callback;
                // BUG-DRIVER-01: Account for the in-flight RunAsync before
                // handing control to ORT so that close() can detect it. The
                // guard undoes the increment on every non-success path
                // (RunAsync returning an error status or throwing); on success
                // the callback takes ownership of the decrement.
                struct InFlightGuard {
                    SessionImage *img;
                    bool committed = false;
                    ~InFlightGuard() { if (!committed && img) --img->inFlightCount; }
                } inflightGuard{image, false};
                ++image->inFlightCount;
                Ort::Status statusRun(Ort::GetApi().RunAsync(
                    image->session, *runOptions, ctx.inputNames.data(), ctx.inputValuePtrs.data(),
                    inputCount, ctx.outputNames.data(), outputCount, ctx.outputValuePtrs.data(),
                    runAsyncCallback, static_cast<void *>(this)));
                if (!statusRun.IsOK()) {
                    // RunAsync failed synchronously: the callback will not be
                    // invoked, so the guard undoes the increment.
                    ctx.releaseOutputValues();
                    if (error) {
                        *error = srt::core::Error(srt::core::Error::SessionError,
                                                  statusRun.GetErrorMessage());
                    }
                    return false;
                }
                inflightGuard.committed = true;
                return true;
            } catch (const Ort::Exception &err) {
                if (error) {
                    *error = srt::core::Error(srt::core::Error::SessionError, err.what());
                }
            } catch (const std::exception &err) {
                if (error) {
                    *error = srt::core::Error(srt::core::Error::SessionError,
                                              std::string("ONNX session run failed: ") + err.what());
                }
            }
            timer.deactivate();
            return false;
        }
    };

    Session::Session() : m_impl(std::make_unique<Impl>()) {
    }

    Session::~Session() {
        close();
    }

    Session::Session(Session &&other) noexcept {
        std::swap(m_impl, other.m_impl);
    }

    Session &Session::operator=(Session &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        std::swap(m_impl, other.m_impl);
        return *this;
    }

    static bool getFileInfo(const fs::path &path, std::vector<uint8_t> &binaryResult,
                            std::string &stringResult, std::streamsize &sizeResult) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }

        // get size
        file.seekg(0, std::ios::end);
        sizeResult = file.tellg();
        file.seekg(0, std::ios::beg);

        static constexpr const size_t buffer_size = 4096; // Process 4KB each time
        char buffer[buffer_size];

        blake3_hasher hasher;
        blake3_hasher_init(&hasher);

        while (file.read(buffer, buffer_size) || file.gcount() > 0) {
            blake3_hasher_update(&hasher, buffer, file.gcount());
        }

        constexpr size_t hashByteSize = 32;

        // get binary
        binaryResult.resize(hashByteSize);
        blake3_hasher_finalize(&hasher, binaryResult.data(), binaryResult.size());

        // get string
        static constexpr char hexDigits[] = "0123456789abcdef";
        stringResult.resize(hashByteSize * 2);
        for (size_t i = 0; i < hashByteSize; ++i) {
            stringResult[2 * i] = hexDigits[binaryResult[i] >> 4];
            stringResult[2 * i + 1] = hexDigits[binaryResult[i] & 0x0f];
        }

        return true;
    }

    srt::core::Expected<void> Session::open(const fs::path &path,
                                            const srt::core::NO<SessionOpenArgs> &args) {
        auto &impl = *m_impl;

        if (isOpen()) {
            Log.srtWarning("Session - Session %1 is already open!", stdc::path::to_utf8(path));
            return srt::core::Error(srt::core::Error::SessionError, "session is already open");
        }

        // Open
        Log.srtDebug("Session - Try open " + stdc::path::to_utf8(path));
        // ROBUST-02: Use error_code overloads to avoid filesystem_error
        // exceptions from fs::is_regular_file and fs::canonical on permission
        // denied or race conditions (e.g., file removed between check and
        // canonicalize). Without this, a transient filesystem error would
        // propagate as an uncaught exception through the Expected<void>
        // boundary.
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) {
            return srt::core::Error(srt::core::Error::FileNotOpen,
                ec ? ("failed to check file: " + ec.message()) : "not a regular file");
        }

        auto canonical_path = fs::canonical(path, ec);
        if (ec) {
            return srt::core::Error(srt::core::Error::FileNotOpen,
                "failed to canonicalize path: " + ec.message());
        }
        Log.srtDebug("Session - The canonical path is " + stdc::path::to_utf8(canonical_path));

        // Ready to load
        auto &session_system = SessionSystem::global();
        std::unique_lock<std::shared_mutex> lock(session_system.mtx);
        SessionImage *image = nullptr;
        std::vector<uint8_t> hash;
        std::streamsize size;

        // Resolve the actual EP/deviceIndex to use for this session.
        // Priority: useCpu=true (force CPU) > args.ep (per-session override)
        // > global Env config. The resolved values are passed to SessionImage
        // so createOrtSession no longer needs to read the global Env.
        ExecutionProvider actualEp;
        int actualDeviceIndex;
        int hints = SH_NoHint;
        if (args->useCpu) {
            hints |= SH_PreferCPUHint;
            actualEp = CPUExecutionProvider; // ignored when SH_PreferCPUHint set
            actualDeviceIndex = -1;
        } else {
            // Resolve EP from args.ep or the global Env config.
            if (args->ep) {
                actualEp = *args->ep;
                const auto devConfig = Env::getDeviceConfig();
                actualDeviceIndex = args->deviceIndex.value_or(devConfig.deviceIndex);
            } else {
                const auto devConfig = Env::getDeviceConfig();
                actualEp = devConfig.ep;
                actualDeviceIndex = devConfig.deviceIndex;
            }
            // Encode the resolved EP/deviceIndex (whether from args.ep or
            // global) into the high bits so that:
            //  - sessions with different EPs get distinct SessionImage cache keys
            //  - SH_EPOverrideHint distinguishes an explicit ep=CPU override
            //    from SH_NoHint (uninitialized), preventing the wrong image
            //    from being returned when ep=CPU+deviceIndex=-1 encodes to 0
            //  - a subsequent Env::setDeviceConfig() change cannot hit a stale
            //    SessionImage cached under the old global EP
            hints |= SH_EPOverrideHint;
            hints |= (static_cast<int>(actualEp) << SH_EPOffset);
            // deviceIndex is clamped to [-1, 254] so that (deviceIndex+1) fits
            // in the 8-bit SH_DeviceMask field without overflow.
            const int clampedDevice =
                std::clamp(actualDeviceIndex, -1, static_cast<int>(0xFF) - 1);
            hints |= ((clampedDevice + 1) << SH_DeviceOffset);
        }
        // Search path
        SessionSystem::ImageGroup *image_group = nullptr;
        if (auto it = session_system.path_map.find(canonical_path);
            it != session_system.path_map.end()) {
            image_group = &(*it->second);
            auto &image_map = image_group->images;
            if (auto it2 = image_map.find(hints); it2 != image_map.end()) {
                auto &data = it2->second;
                image = data.image;
                data.count++;
                goto out_exists;
            }
            hash = it->second->hash;
            size = it->second->size;

            Log.srtDebug("Session - No same hint in opened sessions");
            goto out_search_hash;
        }

        // Calculate hash
        {
            std::string hash_str;
            if (!getFileInfo(canonical_path, hash, hash_str, size)) {
                return srt::core::Error(srt::core::Error::FileNotOpen, "failed to read file");
            }
            Log.srtDebug("Session - BLAKE3 hash is %1", hash_str);
        }

        // Search hash
        if (auto it = session_system.hash_size_map.find({size, hash});
            it != session_system.hash_size_map.end()) {
            image_group = &(*it->second);
            auto &image_map = image_group->images;
            if (auto it2 = image_map.find(hints); it2 != image_map.end()) {
                auto &data = it2->second;
                image = data.image;
                data.count++;
                goto out_exists;
            }
        }

    out_search_hash:

        Log.srtDebug("Session - The session image does not exist. Creating a new one...");

        // Create new one
        image = new SessionImage();
        if (std::string error1;
            !image->open(canonical_path, hints, actualEp, actualDeviceIndex, &error1)) {
            delete image;
            return srt::core::Error{
                srt::core::Error::FileNotOpen,
                "failed to read file: " + error1,
            };
        }

        // Insert
        if (!image_group) {
            Log.srtDebug("Session - The session image group doesn't exist. Creating a new group.");

            SessionSystem::ImageGroup group;
            group.path = canonical_path;
            group.size = size;
            group.hash = std::move(hash);

            auto it = session_system.image_list.emplace(session_system.image_list.end(),
                                                        std::move(group));
            session_system.path_map[it->path] = it;
            session_system.hash_size_map[{size, it->hash}] = it;
            image_group = &(*it);
        }
        image_group->images[hints] = {image, 1};
        goto out_success;

    out_exists:
        Log.srtDebug(
            "Session - The session image already exists. Increasing the reference count...");

    out_success:
        impl.group = image_group;
        impl.image = image;
        impl.hints = hints;
        impl.realPath = canonical_path;
        return srt::core::Expected<void>();
    }

    srt::core::Expected<void> Session::close() {
        auto &impl = *m_impl;

        if (!impl.group)
            return srt::core::Error(srt::core::Error::SessionError, "session is not open");

        const auto &path = impl.realPath;
        const auto &filename = path.filename();
        Log.srtDebug("Session [%1] - close", filename);

        auto &session_system = SessionSystem::global();
        std::unique_lock<std::shared_mutex> lock(session_system.mtx);

        auto &group = *impl.group;
        auto &images = group.images;
        {
            auto it = images.find(impl.hints);
            assert(it != images.end());
            auto &data = it->second;
            if (--data.count != 0) {
                Log.srtDebug("SessionImage [%1] - ref(), now ref count = %2", filename, data.count);
                goto out_success;
            }
            Log.srtDebug("SessionImage [%1] - delete", filename);
            // BUG-DRIVER-01: Refuse to delete the image (which owns the
            // Ort::Session) while an async RunAsync is still using it.
            // Callers must wait for the async callback to fire before
            // calling close(). Simplified scheme: fail loudly instead of
            // blocking here, to avoid synchronizing with ORT internal
            // threads. ErrorCode::SessionBusy is not defined in the public
            // enum, so reuse SessionError with a descriptive message.
            if (data.image && data.image->inFlightCount.load() > 0) {
                // Restore the ref count we just decremented so a later
                // close() after the async run completes can succeed.
                ++data.count;
                return srt::core::Error(srt::core::Error::SessionError,
                                        "cannot close session: async run in progress");
            }
            delete it->second.image;
            images.erase(it);
        }
        if (images.empty()) {
            Log.srtDebug("Session - The session image group is empty. Destroying.");
            auto it = session_system.hash_size_map.find({group.size, group.hash});
            assert(it != session_system.hash_size_map.end());

            auto list_it = it->second;

            session_system.hash_size_map.erase(it);
            session_system.path_map.erase(group.path);
            session_system.image_list.erase(list_it);
        }

    out_success:
        impl.group = nullptr;
        impl.image = nullptr;
        impl.hints = 0;
        impl.realPath.clear();
        return srt::core::Expected<void>();
    }

    const std::filesystem::path &Session::path() const {
        auto &impl = *m_impl;
        return impl.realPath;
    }

    bool Session::isOpen() const {
        auto &impl = *m_impl;
        return impl.group != nullptr;
    }

    static std::vector<std::string> &shared_empty_names() {
        static std::vector<std::string> instance;
        return instance;
    }

    const std::vector<std::string> &Session::inputNames() const {
        auto &impl = *m_impl;
        if (!impl.image) {
            return shared_empty_names();
        }
        return impl.image->inputNames;
    }

    const std::vector<std::string> &Session::outputNames() const {
        auto &impl = *m_impl;
        if (!impl.image) {
            return shared_empty_names();
        }
        return impl.image->outputNames;
    }

    bool Session::terminate() {
        auto &impl = *m_impl;
        // BUG-DRIVER-07: Protect the runOptions pointer read against
        // concurrent sessionRun()/sessionRunAsync() which may (re)create it.
        // A shared_lock is sufficient because RunOptions::SetTerminate is
        // thread-safe by design and may be called concurrently with an
        // in-flight Run/RunAsync; multiple terminate() calls may also overlap.
        std::shared_lock<std::shared_mutex> lock(impl.m_stateMutex);
        if (impl.runOptions) {
            impl.runOptions->SetTerminate();
            return true;
        }
        // runOptions 为 null 表示 session 未 open 或未运行，stop() 无意义
        return false;
    }

    srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
        Session::run(const srt::core::NO<srt::core::TaskStartInput> &input) {
        auto &impl = *m_impl;
        srt::core::Error tmpError;
        if (!(input && input->objectName() == API_NAME)) {
            tmpError = {srt::core::Error::InvalidArgument, "invalid task start input"};
            // BUG-DRIVER-02: Serialize sessionResult writes against
            // result()/runAsyncCallback() readers.
            std::unique_lock<std::shared_mutex> lock(impl.m_stateMutex);
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        if (!impl.group) {
            tmpError = {srt::core::Error::SessionError, "session is not open"};
            std::unique_lock<std::shared_mutex> lock(impl.m_stateMutex);
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        auto startInput = input.as<SessionStartInput>();
        auto result = impl.sessionRun(startInput, &tmpError);
        if (!result) {
            std::unique_lock<std::shared_mutex> lock(impl.m_stateMutex);
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        {
            std::unique_lock<std::shared_mutex> lock(impl.m_stateMutex);
            impl.sessionResult = result;
        }
        return result;
    }

    srt::core::Expected<void>
        Session::runAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                          const srt::core::ITask::StartAsyncCallback &callback) {
        auto &impl = *m_impl;
        srt::core::Error tmpError;
        if (!(input && input->objectName() == API_NAME)) {
            tmpError = {srt::core::Error::InvalidArgument, "invalid task start input"};
            // BUG-DRIVER-02: Serialize sessionResult writes against
            // result()/runAsyncCallback() readers.
            std::unique_lock<std::shared_mutex> lock(impl.m_stateMutex);
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        if (!impl.group) {
            tmpError = {srt::core::Error::SessionError, "session is not open"};
            std::unique_lock<std::shared_mutex> lock(impl.m_stateMutex);
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        auto startInput = input.as<SessionStartInput>();
        bool ok = impl.sessionRunAsync(startInput, callback, &tmpError);
        if (!ok) {
            std::unique_lock<std::shared_mutex> lock(impl.m_stateMutex);
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        return srt::core::Expected<void>();
    }

    srt::core::NO<srt::core::TaskResult> Session::result() const {
        auto &impl = *m_impl;
        // BUG-DRIVER-02: Take a shared_lock so that concurrent writers in
        // run()/runAsync()/runAsyncCallback() cannot tear the sessionResult
        // pointer while we read it. Multiple result() calls may overlap.
        std::shared_lock<std::shared_mutex> lock(impl.m_stateMutex);
        return impl.sessionResult.as<srt::core::TaskResult>();
    }
}
