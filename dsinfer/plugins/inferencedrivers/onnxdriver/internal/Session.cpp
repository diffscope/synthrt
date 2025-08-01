#include "Session.h"

#include <cassert>
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <fstream>
#include <unordered_set>
#include <algorithm>
#include <list>
#include <numeric>

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>

#include <synthrt/synthrt_global.h>
#include <synthrt/Support/Expected.h>

#include <blake3.h>

#include "OnnxDriver_Logger.h"
#include "SessionImage.h"
#include "ScopedTimer.h"

#include "OnnxTensor.h"


namespace fs = std::filesystem;

namespace ds::onnxdriver {

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
        srt::ITask::StartAsyncCallback callback;
    };

    class Session::Impl {
    public:
        Ort::RunOptions runOptions;

        SessionSystem::ImageGroup *group = nullptr;
        SessionImage *image = nullptr;
        int hints = 0;

        std::filesystem::path realPath;

        std::unique_ptr<SessionRunContext> context;
        std::unique_ptr<SessionAsyncRunContext> asyncContext;
        srt::NO<Api::Onnx::SessionResult> sessionResult;

        Impl() : sessionResult(srt::NO<Api::Onnx::SessionResult>::create()) {
        }

        static inline size_t getTensorDataTypeSize(ITensor::DataType type) {
            switch (type) {
                case ITensor::Float:
                    return sizeof(float);
                case ITensor::Int64:
                    return sizeof(int64_t);
                case ITensor::Bool:
                    return sizeof(bool);
                default:
                    return 0; // error
            }
        }

        template <typename T>
        static inline ITensor::DataType getTensorDataType() {
            if constexpr (std::is_same_v<T, float>) {
                return ITensor::Float;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return ITensor::Int64;
            } else if constexpr (std::is_same_v<T, bool>) {
                return ITensor::Bool;
            } else {
                static_assert(sizeof(T) == 0, "Unsupported type for getTensorDType");
                return ITensor::Float; // fallback to avoid warnings, won't compile anyway due to
                                       // static_assert
            }
        }

        template <typename T>
        static inline Ort::Value
            _createOrtValueFromTensorImpl(const std::byte *rawBuffer, const size_t dataLength,
                                          const stdc::array_view<int64_t> shape) {
            // Caller should ensure dataLength matches shape
            const auto dataBuffer = reinterpret_cast<const T *>(rawBuffer);
            auto ortTensor = Ort::Value::CreateTensor<T>(Ort::AllocatorWithDefaultOptions{},
                                                         shape.data(), shape.size());
            auto ortTensorBuffer = ortTensor.template GetTensorMutableData<T>();
            std::memcpy(ortTensorBuffer, dataBuffer, dataLength * sizeof(T));
            return ortTensor;
        }

        static inline Ort::Value createOrtValueFromTensor(const srt::NO<ITensor> &tensor,
                                                          const Ort::MemoryInfo &memoryInfo,
                                                          srt::Error *error = nullptr) {
            const auto &rawBuffer = tensor->rawData();
            const auto dtype = tensor->dataType();
            auto shape = tensor->shape();
            auto dataLength = tensor->elementCount();
            auto dataLengthFromShape =
                std::accumulate(shape.begin(), shape.end(), int64_t{1}, std::multiplies<>());
            if (dataLength != dataLengthFromShape) {
                if (error) {
                    *error = {srt::Error::InvalidArgument, "Shape does not match data length"};
                }
                return Ort::Value(nullptr);
            }
            switch (dtype) {
                case ITensor::Float:
                    return _createOrtValueFromTensorImpl<float>(rawBuffer, dataLength, shape);
                case ITensor::Int64:
                    return _createOrtValueFromTensorImpl<int64_t>(rawBuffer, dataLength,
                                                                  shape);
                case ITensor::Bool:
                    return _createOrtValueFromTensorImpl<bool>(rawBuffer, dataLength, shape);
                default:
                    if (error) {
                        *error = {srt::Error::InvalidArgument, "Unsupported data type"};
                    }
                    return Ort::Value(nullptr);
            }
        }

        static inline srt::NO<ITensor> createTensorFromOrtValue(const Ort::Value &ortValue,
                                                                srt::Error *error = nullptr) {
            if (!ortValue.IsTensor()) {
                if (error) {
                    *error = {srt::Error::InvalidArgument, "Ort::Value is not a tensor"};
                }
                return {};
            }

            auto typeInfo = ortValue.GetTensorTypeAndShapeInfo();
            auto shape = typeInfo.GetShape();
            auto elemType = typeInfo.GetElementType();
            auto totalSize = typeInfo.GetElementCount();

            ITensor::DataType tensorType;
            size_t elementSize;

            switch (elemType) {
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
                    tensorType = ITensor::Float;
                    elementSize = sizeof(float);
                    break;
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
                    tensorType = ITensor::Int64;
                    elementSize = sizeof(int64_t);
                    break;
                case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:
                    tensorType = ITensor::Bool;
                    elementSize = sizeof(bool);
                    break;
                default:
                    if (error) {
                        *error = {srt::Error::InvalidArgument,
                                  "Unsupported ONNX tensor element type"};
                    }
                    return {};
            }

            auto rawData =
                static_cast<const std::byte *>(ortValue.GetTensorData<void>());
            stdc::array_view<std::byte> data{rawData, rawData + totalSize * elementSize};

            if (auto exp = Tensor::createFromRawView(tensorType, shape, data); exp) {
                return exp.take();
            } else {
                if (error) {
                    *error = exp.takeError();
                }
                return {};
            }
        }

        inline srt::Error
            validateInputValueMap(const srt::NO<Api::Onnx::SessionStartInput> &input) {
            const auto &inputValueMap = input->inputs;
            if (inputValueMap.empty()) {
                return {srt::Error::SessionError, "Input map is empty"};
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
                    return {srt::Error::SessionError, msgStream.str()};
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
                impl.sessionResult->error = {srt::Error::SessionError, runStatus.GetErrorMessage()};
                impl.asyncContext->callback(impl.sessionResult, impl.sessionResult->error);
                srtCritical("runAsyncCallback failed");
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
            srtDebug("runAsyncCallback completed");
        }

        inline srt::NO<Api::Onnx::SessionResult> sessionRun(const srt::NO<Api::Onnx::SessionStartInput> &sessionStartInput,
                               srt::Error *error = nullptr) {
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

            if (!(sessionStartInput && sessionStartInput->objectName() == Api::Onnx::API_NAME)) {
                if (error) {
                    *error = {srt::Error::InvalidArgument, "Session start input is not valid"};
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

            auto result = srt::NO<Api::Onnx::SessionResult>::create();
            try {
                auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

                for (auto &[name, value] : inputValueMap) {
                    ctx.inputNames.push_back(name.c_str());
                    if (value->backend() == "tensor") {
                        auto ortValue = createOrtValueFromTensor(value, memInfo, error);
                        if (!ortValue) {
                            if (error) {
                                *error = {srt::Error::InvalidArgument,
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
                            *error = {srt::Error::InvalidArgument,
                                      "Unknown tensor backend for input name \"" + name + "\""};
                        }
                        return {};
                    }
                }

                for (auto &name : sessionStartInput->outputs) {
                    ctx.outputNames.push_back(name.c_str());
                }
                runOptions.UnsetTerminate();

                Ort::Status statusRun(Ort::GetApi().Run(
                    image->session, runOptions, ctx.inputNames.data(), ctx.inputValuePtrs.data(),
                    inputCount, ctx.outputNames.data(), outputCount, ctx.outputValuePtrs.data()));

                if (!statusRun.IsOK()) {
                    ctx.releaseOutputValues();
                    if (error) {
                        *error = srt::Error(srt::Error::SessionError, statusRun.GetErrorMessage());
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
                    *error = srt::Error(srt::Error::SessionError, err.what());
                }
            }
            timer.deactivate();
            return {};
        }

        inline bool sessionRunAsync(const srt::NO<Api::Onnx::SessionStartInput> &sessionStartInput,
                                    const srt::ITask::StartAsyncCallback &callback,
                                    srt::Error *error = nullptr) {
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

            if (!(sessionStartInput && sessionStartInput->objectName() == Api::Onnx::API_NAME)) {
                if (error) {
                    *error = {srt::Error::InvalidArgument, "Session start input is not valid"};
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
                auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

                for (auto &[name, value] : inputValueMap) {
                    ctx.inputNames.push_back(name.c_str());
                    if (value->backend() == "tensor") {
                        auto ortValue = createOrtValueFromTensor(value, memInfo, error);
                        if (!ortValue) {
                            if (error) {
                                *error = {srt::Error::InvalidArgument,
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
                            *error = {srt::Error::InvalidArgument,
                                      "Unknown tensor backend for input name \"" + name + "\""};
                        }
                        return false;
                    }
                }

                for (auto &name : sessionStartInput->outputs) {
                    ctx.outputNames.push_back(name.c_str());
                }
                runOptions.UnsetTerminate();

                asyncContext->callback = callback;
                Ort::Status statusRun(Ort::GetApi().RunAsync(
                    image->session, runOptions, ctx.inputNames.data(), ctx.inputValuePtrs.data(),
                    inputCount, ctx.outputNames.data(), outputCount, ctx.outputValuePtrs.data(),
                    runAsyncCallback, static_cast<void *>(this)));
                if (!statusRun.IsOK()) {
                    ctx.releaseOutputValues();
                    if (error) {
                        *error = srt::Error(srt::Error::SessionError, statusRun.GetErrorMessage());
                    }
                    return false;
                }
                return true;
            } catch (const Ort::Exception &err) {
                if (error) {
                    *error = srt::Error(srt::Error::SessionError, err.what());
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

    srt::Expected<void> Session::open(const fs::path &path,
                                      const srt::NO<Api::Onnx::SessionOpenArgs> &args) {
        __stdc_impl_t;

        if (isOpen()) {
            Log.srtWarning("Session - Session %1 is already open!", path.string());
            return srt::Error(srt::Error::SessionError, "session is already open");
        }

        // Open
        Log.srtDebug("Session - Try open " + path.string());
        if (!fs::is_regular_file(path)) {
            return srt::Error(srt::Error::FileNotOpen, "not a regular file");
        }

        fs::path canonical_path = fs::canonical(path);
        Log.srtDebug("Session - The canonical path is " + canonical_path.string());

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
                return srt::Error(srt::Error::FileNotOpen, "failed to read file");
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
            return srt::Error{
                srt::Error::FileNotOpen,
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
        return srt::Expected<void>();
    }

    srt::Expected<void> Session::close() {
        __stdc_impl_t;

        if (!impl.group)
            return srt::Error(srt::Error::SessionError, "session is not open");

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
        return srt::Expected<void>();
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
        impl.runOptions.SetTerminate();
    }

    srt::Expected<srt::NO<srt::TaskResult>> Session::run(const srt::NO<srt::TaskStartInput> &input) {
        __stdc_impl_t;
        srt::Error tmpError;
        if (!(input && input->objectName() == Api::Onnx::API_NAME)) {
            tmpError = {srt::Error::InvalidArgument, "invalid task start input"};
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        if (!impl.group) {
            tmpError = {srt::Error::SessionError, "session is not open"};
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        auto startInput = input.as<Api::Onnx::SessionStartInput>();
        auto result = impl.sessionRun(startInput, &tmpError);
        if (!result) {
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        impl.sessionResult = result;
        return result;
    }

    srt::Expected<void> Session::runAsync(const srt::NO<srt::TaskStartInput> &input,
                                          const srt::ITask::StartAsyncCallback &callback) {
        __stdc_impl_t;
        srt::Error tmpError;
        if (!(input && input->objectName() == Api::Onnx::API_NAME)) {
            tmpError = {srt::Error::InvalidArgument, "invalid task start input"};
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        if (!impl.group) {
            tmpError = {srt::Error::SessionError, "session is not open"};
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        auto startInput = input.as<Api::Onnx::SessionStartInput>();
        bool ok = impl.sessionRunAsync(startInput, callback, &tmpError);
        if (!ok) {
            impl.sessionResult->error = tmpError;
            return tmpError;
        }
        return srt::Expected<void>();
    }

    srt::NO<srt::TaskResult> Session::result() const {
        __stdc_impl_t;
        return impl.sessionResult.as<srt::TaskResult>();
    }
}