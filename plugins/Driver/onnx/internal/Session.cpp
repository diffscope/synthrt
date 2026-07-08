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

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>

#include <synthrt/Core/srt_core_global.h>
#include <synthrt/Core/Support/Expected.h>

#include <blake3.h>

#include "OnnxDriver_Logger.h"
#include "SessionImage.h"
#include "ScopedTimer.h"

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
                if (!runOptions) {
                    runOptions = std::make_unique<Ort::RunOptions>();
                }
                runOptions->UnsetTerminate();

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
                sessionResult = result;
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
                if (!runOptions) {
                    runOptions = std::make_unique<Ort::RunOptions>();
                }
                runOptions->UnsetTerminate();

                asyncContext->callback = callback;
                Ort::Status statusRun(Ort::GetApi().RunAsync(
                    image->session, *runOptions, ctx.inputNames.data(), ctx.inputValuePtrs.data(),
                    inputCount, ctx.outputNames.data(), outputCount, ctx.outputValuePtrs.data(),
                    runAsyncCallback, static_cast<void *>(this)));
                if (!statusRun.IsOK()) {
                    ctx.releaseOutputValues();
                    if (error) {
                        *error = srt::core::Error(srt::core::Error::SessionError,
                                                  statusRun.GetErrorMessage());
                    }
                    return false;
                }
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

    Session::Session() : _impl(std::make_unique<Impl>()) {
    }

    Session::~Session() {
        close();
    }

    Session::Session(Session &&other) noexcept {
        std::swap(_impl, other._impl);
    }

    Session &Session::operator=(Session &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        std::swap(_impl, other._impl);
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
        __stdc_impl_t;

        if (isOpen()) {
            Log.srtWarning("Session - Session %1 is already open!", stdc::path::to_utf8(path));
            return srt::core::Error(srt::core::Error::SessionError, "session is already open");
        }

        // Open
        Log.srtDebug("Session - Try open " + stdc::path::to_utf8(path));
        if (!fs::is_regular_file(path)) {
            return srt::core::Error(srt::core::Error::FileNotOpen, "not a regular file");
        }

        fs::path canonical_path = fs::canonical(path);
        Log.srtDebug("Session - The canonical path is " + stdc::path::to_utf8(canonical_path));

        // Ready to load
        auto &session_system = SessionSystem::global();
        std::unique_lock<std::shared_mutex> lock(session_system.mtx);
        SessionImage *image = nullptr;
        std::vector<uint8_t> hash;
        std::streamsize size;

        int hints = SH_NoHint;
        if (args->useCpu) {
            hints |= SH_PreferCPUHint;
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
        if (std::string error1; !image->open(canonical_path, hints, &error1)) {
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
        __stdc_impl_t;

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
        __stdc_impl_t;
        return impl.realPath;
    }

    bool Session::isOpen() const {
        __stdc_impl_t;
        return impl.group != nullptr;
    }

    static std::vector<std::string> &shared_empty_names() {
        static std::vector<std::string> instance;
        return instance;
    }

    const std::vector<std::string> &Session::inputNames() const {
        __stdc_impl_t;
        if (!impl.image) {
            return shared_empty_names();
        }
        return impl.image->inputNames;
    }

    const std::vector<std::string> &Session::outputNames() const {
        __stdc_impl_t;
        if (!impl.image) {
            return shared_empty_names();
        }
        return impl.image->outputNames;
    }

    void Session::terminate() {
        __stdc_impl_t;
        if (impl.runOptions) {
            impl.runOptions->SetTerminate();
        }
    }

    srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
        Session::run(const srt::core::NO<srt::core::TaskStartInput> &input) {
        __stdc_impl_t;
        srt::core::Error tmpError;
        if (!(input && input->objectName() == API_NAME)) {
            tmpError = {srt::core::Error::InvalidArgument, "invalid task start input"};
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        if (!impl.group) {
            tmpError = {srt::core::Error::SessionError, "session is not open"};
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        auto startInput = input.as<SessionStartInput>();
        auto result = impl.sessionRun(startInput, &tmpError);
        if (!result) {
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        impl.sessionResult = result;
        return result;
    }

    srt::core::Expected<void>
        Session::runAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                          const srt::core::ITask::StartAsyncCallback &callback) {
        __stdc_impl_t;
        srt::core::Error tmpError;
        if (!(input && input->objectName() == API_NAME)) {
            tmpError = {srt::core::Error::InvalidArgument, "invalid task start input"};
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        if (!impl.group) {
            tmpError = {srt::core::Error::SessionError, "session is not open"};
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        auto startInput = input.as<SessionStartInput>();
        bool ok = impl.sessionRunAsync(startInput, callback, &tmpError);
        if (!ok) {
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        return srt::core::Expected<void>();
    }

    srt::core::NO<srt::core::TaskResult> Session::result() const {
        __stdc_impl_t;
        return impl.sessionResult.as<srt::core::TaskResult>();
    }
}
