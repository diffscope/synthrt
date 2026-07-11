#include "GameExtractor.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <tuple>

#include <nlohmann/json.hpp>

#include <synthrt/Audio/Slicer.h>
#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Tensor/ITensor.h>
#include <synthrt/Core/Tensor/Tensor.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <synthrt/Extract/AudioPreprocessor.h>
#include <synthrt/Extract/ExtractorDriver.h>

// ============================================================================
// 文件局部辅助函数（从 GameModel.cpp 迁移）
// ============================================================================

namespace {

    /// 从 bool vector 创建 Bool 类型的 Tensor（从 GameModel::createFromBoolVector 迁移）
    srt::core::Expected<srt::core::NO<srt::core::Tensor>>
    createBoolTensor(const std::vector<int64_t> &shape, const std::vector<bool> &boolVec) {
        size_t totalElements = 1;
        for (const int64_t dim : shape) {
            totalElements *= static_cast<size_t>(dim);
        }
        if (totalElements != boolVec.size()) {
            return srt::core::Error(srt::core::ErrorCode::ExtractOutputInvalid,
                                    "createBoolTensor: shape doesn't match data size");
        }

        srt::core::Tensor::Container dataContainer;
        dataContainer.reserve(boolVec.size());
        for (const bool b : boolVec) {
            dataContainer.push_back(static_cast<std::byte>(b ? 1 : 0));
        }

        return srt::core::Tensor::createFromRawData(
            srt::core::ITensor::Bool,
            {1, static_cast<int64_t>(dataContainer.size())},
            std::move(dataContainer));
    }

    /// 从 session 输出中提取 float vector（从 GameModel::extractTensor<float> 迁移）
    srt::core::Expected<std::vector<float>>
    extractFloatTensor(const std::map<std::string, srt::core::NO<srt::core::ITensor>> &outputs,
                       const std::string &name) {
        const auto it = outputs.find(name);
        if (it == outputs.end()) {
            return srt::core::Error(srt::core::ErrorCode::ExtractOutputInvalid,
                                    "missing output: " + name);
        }
        const auto &tensor = it->second;
        if (tensor->dataType() != srt::core::tensor_traits<float>::data_type) {
            return srt::core::Error(srt::core::ErrorCode::ExtractOutputInvalid,
                                    "data type mismatch: " + name);
        }
        const auto data = tensor->view<float>();
        if (data.empty()) {
            return srt::core::Error(srt::core::ErrorCode::ExtractOutputInvalid,
                                    "could not get output data: " + name);
        }
        return data.vec();
    }

    /// 计算 MarkerList 中所有切片帧数总和（原 calculateSumOfDifferences）
    uint64_t calculateSumOfDifferences(const srt::audio::MarkerList &markers) {
        uint64_t sum = 0;
        for (const auto &[start, end] : markers) {
            sum += static_cast<uint64_t>(end - start);
        }
        return sum;
    }

} // namespace

// ============================================================================
// GameExtractor 公共方法
// ============================================================================

GameExtractor::GameExtractor(srt::core::Runtime *runtime) : m_runtime(runtime) {
}

GameExtractor::~GameExtractor() {
    close();
}

srt::core::Expected<void> GameExtractor::open(const std::filesystem::path &modelPath) {
    try {
        // 1. 读取 config.json
        const auto configPath = modelPath / "config.json";
        std::ifstream configFile(configPath);
        if (!configFile.is_open()) {
            return srt::core::Error(
                srt::core::ErrorCode::ExtractModelOpenFailed,
                "Could not open config.json: " + configPath.string());
        }
        nlohmann::json config;
        configFile >> config;
        configFile.close();

        // 2. 加载参数（从 GameModel::open 迁移）
        m_timestep = config.value("timestep", 0.01f);
        m_targetSampleRate = config.value("samplerate", 44100);
        m_segThreshold = config.value("seg_threshold", 0.2f);
        m_segRadiusSeconds = config.value("seg_radius_seconds", 0.02f);
        m_estThreshold = config.value("est_threshold", 0.2f);

        if (config.contains("languages")) {
            m_language = 0;
        }

        // 3. 获取 ONNX 推理驱动
        auto driverExp = srt::extract::getInferenceDriver(m_runtime);
        if (!driverExp) {
            return srt::core::Error(
                srt::core::ErrorCode::ExtractModelOpenFailed,
                "Failed to get inference driver: " + driverExp.error().message());
        }
        m_driver = driverExp.take();

        // 4. 加载 4 个 ONNX session
        auto loadSession = [&](const std::string &name) ->
            srt::core::Expected<srt::core::NO<srt::driver::InferenceSession>> {
            const auto modelFile = modelPath / name;
            auto session = m_driver->createSession();
            if (!session) {
                return srt::core::Error(
                    srt::core::ErrorCode::ExtractModelOpenFailed,
                    "open: could not create session for " + name +
                        " (model dir: " + modelPath.string() + ")");
            }
            auto openExp = session->open(
                modelFile, srt::core::NO<srt::driver::onnx::SessionOpenArgs>::create());
            if (!openExp) {
                return openExp.takeError();
            }
            return session;
        };

        auto encoderExp = loadSession("encoder.onnx");
        if (!encoderExp) {
            return srt::core::Error(
                srt::core::ErrorCode::ExtractModelOpenFailed,
                "open: failed to load encoder.onnx: " + encoderExp.error().message());
        }
        m_encoder = encoderExp.take();

        auto segmenterExp = loadSession("segmenter.onnx");
        if (!segmenterExp) {
            return srt::core::Error(
                srt::core::ErrorCode::ExtractModelOpenFailed,
                "open: failed to load segmenter.onnx: " + segmenterExp.error().message());
        }
        m_segmenter = segmenterExp.take();

        auto estimatorExp = loadSession("estimator.onnx");
        if (!estimatorExp) {
            return srt::core::Error(
                srt::core::ErrorCode::ExtractModelOpenFailed,
                "open: failed to load estimator.onnx: " + estimatorExp.error().message());
        }
        m_estimator = estimatorExp.take();

        auto bd2durExp = loadSession("bd2dur.onnx");
        if (!bd2durExp) {
            return srt::core::Error(
                srt::core::ErrorCode::ExtractModelOpenFailed,
                "open: failed to load bd2dur.onnx: " + bd2durExp.error().message());
        }
        m_bd2dur = bd2durExp.take();

        return srt::core::Expected<void>();
    } catch (const std::exception &e) {
        return srt::core::Error(
            srt::core::ErrorCode::ExtractModelOpenFailed,
            std::string("GameExtractor::open: ") + e.what());
    }
}

bool GameExtractor::isOpen() const {
    return m_encoder != nullptr && m_segmenter != nullptr &&
           m_estimator != nullptr && m_bd2dur != nullptr;
}

void GameExtractor::close() {
    auto closeSession = [](srt::core::NO<srt::driver::InferenceSession> &session) {
        if (session) {
            session->stop();
            session->close();
            session.reset();
        }
    };
    closeSession(m_encoder);
    closeSession(m_segmenter);
    closeSession(m_estimator);
    closeSession(m_bd2dur);
    m_driver.reset();
}

void GameExtractor::terminate() {
    if (m_encoder) m_encoder->stop();
    if (m_segmenter) m_segmenter->stop();
    if (m_estimator) m_estimator->stop();
    if (m_bd2dur) m_bd2dur->stop();
}

srt::extract::AudioRequirements GameExtractor::audioRequirements() const {
    return {m_targetSampleRate, 1};
}

srt::core::Expected<srt::extract::MidiResult>
GameExtractor::extract(const srt::audio::AudioBuffer &buffer,
                       int sampleRate,
                       const srt::extract::MidiExtractOptions &options,
                       const srt::extract::ProgressCallback &progress) {
    if (!isOpen()) {
        return srt::core::Error(srt::core::ErrorCode::ExtractNotInitialized,
                                "GameExtractor is not opened");
    }

    try {
        // 1. 确定 effective parameters（options 覆盖 config.json 默认值）
        const float tempo = options.tempo;
        const float segThreshold =
            (options.segThreshold != 0.2f) ? options.segThreshold : m_segThreshold;
        const float estThreshold =
            (options.estThreshold != 0.2f) ? options.estThreshold : m_estThreshold;
        const int language =
            (options.language != 0) ? options.language : m_language;
        const float segRadiusSeconds = options.segRadiusSeconds;
        const std::vector<float> d3pmTs =
            options.d3pmTs.empty() ? generateD3pmTs() : options.d3pmTs;

        int segRadiusFrames = static_cast<int>(std::round(segRadiusSeconds / m_timestep));
        if (segRadiusFrames < 1) segRadiusFrames = 1;

        // 2. 重采样到 tar_sr mono（用 AudioPreprocessor）
        const auto req = audioRequirements();
        auto monoExp = srt::extract::AudioPreprocessor::resampleToMono(buffer, sampleRate, req);
        if (!monoExp) {
            return monoExp.takeError();
        }
        auto [audio, outSampleRate] = monoExp.take();

        // 3. RMS 切片（参数从 Game.cpp:107 迁移）
        srt::audio::Slicer slicer(m_targetSampleRate, 0.02f, 441, 1764, 200, 30, 50);
        const auto chunks = slicer.slice(audio);

        if (chunks.empty()) {
            return srt::core::Error(srt::core::ErrorCode::ExtractOutputInvalid,
                                    "extract: slicer produced no audio chunks");
        }

        // 4. 逐切片推理 + 构建 MIDI 音符
        srt::extract::MidiResult result;
        const auto totalSize = static_cast<int64_t>(audio.size());
        const auto slicerFrames = calculateSumOfDifferences(chunks);
        int processedFrames = 0;

        for (const auto &[beginFrame, endFrame] : chunks) {
            const auto frameCount = endFrame - beginFrame;
            const double sliceDuration = static_cast<double>(frameCount) / m_targetSampleRate;

            if (sliceDuration > 60.0) {
                return srt::core::Error(
                    srt::core::ErrorCode::ExtractOutputInvalid,
                    "Slice duration exceeds 60 seconds: " + std::to_string(sliceDuration) +
                        "s. Please check whether the accompaniment has been removed.");
            }

            if (frameCount <= 0 || beginFrame > totalSize || endFrame > totalSize) {
                continue;
            }

            // 提取切片音频
            std::vector<float> sliceAudio(audio.begin() + beginFrame, audio.begin() + endFrame);
            const float duration = static_cast<float>(sliceAudio.size()) /
                                   static_cast<float>(m_targetSampleRate);

            // 4 阶段推理
            const auto output = inferSlice(sliceAudio, duration, language,
                                           segThreshold, segRadiusFrames,
                                           estThreshold, d3pmTs);

            // 计算 start_tick（从 Game.cpp:149-152 迁移）
            int previousEndTick = 0;
            if (!result.notes.empty()) {
                const auto &last = result.notes.back();
                previousEndTick = last.start + last.duration;
            }
            int chunkStartTicks = static_cast<int>(std::round(
                static_cast<double>(beginFrame) / m_targetSampleRate * tempo * 480.0 / 60.0));
            int startTick = std::max(chunkStartTicks, previousEndTick);

            // 构建 MIDI 音符
            auto sliceNotes = buildMidiNotes(startTick, output.durations,
                                             output.presence, output.scores, tempo);
            result.notes.insert(result.notes.end(), sliceNotes.begin(), sliceNotes.end());

            // 进度回调
            processedFrames += static_cast<int>(frameCount);
            if (progress && slicerFrames > 0) {
                progress(static_cast<int>(
                    static_cast<float>(processedFrames) / static_cast<float>(slicerFrames) * 100));
            }
        }

        return result;
    } catch (const std::exception &e) {
        return srt::core::Error(
            srt::core::ErrorCode::ExtractInferenceFailed,
            std::string("GameExtractor::extract: ") + e.what());
    }
}

// ============================================================================
// 4 阶段推理（从 GameModel.cpp 迁移）
// ============================================================================

std::tuple<srt::core::NO<srt::core::ITensor>,
           srt::core::NO<srt::core::ITensor>,
           srt::core::NO<srt::core::ITensor>>
GameExtractor::runEncoder(const std::vector<float> &waveform, float duration) const {
    if (waveform.empty() || !m_encoder) {
        return {nullptr, nullptr, nullptr};
    }

    const std::vector<int64_t> waveformShape = {1, static_cast<int64_t>(waveform.size())};
    const std::vector<int64_t> durationShape = {1};

    const auto sessionInput = srt::core::NO<srt::driver::onnx::SessionStartInput>::create();

    // waveform 输入
    auto waveformExp = srt::core::Tensor::createFromView<float>(waveformShape, stdc::array_view<float>{waveform});
    if (!waveformExp) {
        throw std::runtime_error("Failed to create waveform tensor: " + waveformExp.error().message());
    }
    sessionInput->inputs["waveform"] = waveformExp.take();

    // duration 输入
    const std::vector<float> durationVec = {duration};
    auto durationExp = srt::core::Tensor::createFromView<float>(durationShape, stdc::array_view<float>{durationVec});
    if (!durationExp) {
        throw std::runtime_error("Failed to create duration tensor: " + durationExp.error().message());
    }
    sessionInput->inputs["duration"] = durationExp.take();

    sessionInput->outputs = {"x_seg", "x_est", "maskT"};

    auto startExp = m_encoder->start(sessionInput);
    if (!startExp) {
        throw std::runtime_error("Failed to run encoder: " + startExp.error().message());
    }
    const auto result = m_encoder->result().as<srt::driver::onnx::SessionResult>();
    if (!result) {
        throw std::runtime_error("Could not get encoder session result");
    }

    const auto xSegIt = result->outputs.find("x_seg");
    const auto xEstIt = result->outputs.find("x_est");
    const auto maskTIt = result->outputs.find("maskT");

    srt::core::NO<srt::core::ITensor> xSeg = (xSegIt != result->outputs.end()) ? xSegIt->second : nullptr;
    srt::core::NO<srt::core::ITensor> xEst = (xEstIt != result->outputs.end()) ? xEstIt->second : nullptr;
    srt::core::NO<srt::core::ITensor> maskT = (maskTIt != result->outputs.end()) ? maskTIt->second : nullptr;

    return {xSeg, xEst, maskT};
}

std::vector<uint8_t> GameExtractor::runSegmenter(
    const srt::core::NO<srt::core::ITensor> &xSeg,
    const std::vector<uint8_t> &knownBoundaries,
    const std::vector<uint8_t> &prevBoundaries,
    int language,
    const srt::core::NO<srt::core::ITensor> &maskT,
    float threshold, int radius,
    const std::vector<float> &d3pmTs) const {

    if (d3pmTs.empty() || !m_segmenter) {
        return prevBoundaries;
    }

    if (!maskT) {
        throw std::runtime_error("runSegmenter: maskT tensor is null");
    }
    auto maskTShape = maskT->shape();
    if (maskTShape.size() != 2 || maskTShape[0] != 1) {
        throw std::runtime_error("runSegmenter: maskT shape unexpected");
    }
    int64_t T = maskTShape[1];
    if (T <= 0) return {};

    // 提取 maskT 数据
    auto maskTData = maskT->rawView();
    std::vector<bool> maskTBool(static_cast<size_t>(T));
    for (int64_t i = 0; i < T; ++i) {
        maskTBool[static_cast<size_t>(i)] = maskTData[static_cast<size_t>(i)] != std::byte{0};
    }

    // 提取 xSeg 数据
    auto xSegShape = xSeg->shape();
    std::vector<float> xSegData;
    auto xSegExp = extractFloatTensor({{"x_seg", xSeg}}, "x_seg");
    if (!xSegExp) {
        throw std::runtime_error("Failed to extract xSeg tensor data: " + xSegExp.error().message());
    }
    xSegData = xSegExp.take();

    std::vector<int64_t> xSegShapeArr = {xSegShape[0], xSegShape[1], xSegShape[2]};

    std::vector<uint8_t> currentBoundaries = prevBoundaries;
    if (currentBoundaries.size() != static_cast<size_t>(T)) {
        currentBoundaries.resize(static_cast<size_t>(T), 0);
    }

    std::vector<uint8_t> knownBd = knownBoundaries;
    if (knownBd.size() != static_cast<size_t>(T)) {
        knownBd.resize(static_cast<size_t>(T), 0);
    }

    // 逐步迭代 d3pmTs
    for (float t : d3pmTs) {
        auto sessionInput = srt::core::NO<srt::driver::onnx::SessionStartInput>::create();

        // x_seg
        auto xSegTensorExp = srt::core::Tensor::createFromView<float>(xSegShapeArr, stdc::array_view<float>{xSegData});
        if (!xSegTensorExp) {
            throw std::runtime_error("Failed to create xSeg tensor: " + xSegTensorExp.error().message());
        }
        sessionInput->inputs["x_seg"] = xSegTensorExp.take();

        // maskT
        auto maskTTensorExp = createBoolTensor({1, static_cast<int64_t>(maskTBool.size())}, maskTBool);
        if (!maskTTensorExp) {
            throw std::runtime_error("Failed to create maskT tensor: " + maskTTensorExp.error().message());
        }
        sessionInput->inputs["maskT"] = maskTTensorExp.take();

        // known_boundaries
        std::vector<bool> knownBdVec(knownBd.begin(), knownBd.end());
        auto knownBdExp = createBoolTensor({1, static_cast<int64_t>(knownBdVec.size())}, knownBdVec);
        if (!knownBdExp) {
            throw std::runtime_error("Failed to create known_boundaries tensor: " + knownBdExp.error().message());
        }
        sessionInput->inputs["known_boundaries"] = knownBdExp.take();

        // prev_boundaries
        std::vector<bool> currentBdVec(currentBoundaries.begin(), currentBoundaries.end());
        auto currentBdExp = createBoolTensor({1, static_cast<int64_t>(currentBdVec.size())}, currentBdVec);
        if (!currentBdExp) {
            throw std::runtime_error("Failed to create prev_boundaries tensor: " + currentBdExp.error().message());
        }
        sessionInput->inputs["prev_boundaries"] = currentBdExp.take();

        // language
        std::vector<int64_t> langVec = {static_cast<int64_t>(language)};
        auto langExp = srt::core::Tensor::createFromView<int64_t>({1}, stdc::array_view<int64_t>{langVec});
        if (!langExp) {
            throw std::runtime_error("Failed to create language tensor: " + langExp.error().message());
        }
        sessionInput->inputs["language"] = langExp.take();

        // threshold
        std::vector<float> threshVec = {threshold};
        auto threshExp = srt::core::Tensor::createFromView<float>({1}, stdc::array_view<float>{threshVec});
        if (!threshExp) {
            throw std::runtime_error("Failed to create threshold tensor: " + threshExp.error().message());
        }
        sessionInput->inputs["threshold"] = threshExp.take();

        // radius
        std::vector<int64_t> radiusVec = {static_cast<int64_t>(radius)};
        auto radiusExp = srt::core::Tensor::createFromView<int64_t>({1}, stdc::array_view<int64_t>{radiusVec});
        if (!radiusExp) {
            throw std::runtime_error("Failed to create radius tensor: " + radiusExp.error().message());
        }
        sessionInput->inputs["radius"] = radiusExp.take();

        // t
        std::vector<float> tVec = {t};
        auto tExp = srt::core::Tensor::createFromView<float>({1}, stdc::array_view<float>{tVec});
        if (!tExp) {
            throw std::runtime_error("Failed to create t tensor: " + tExp.error().message());
        }
        sessionInput->inputs["t"] = tExp.take();

        sessionInput->outputs = {"boundaries"};

        auto startExp = m_segmenter->start(sessionInput);
        if (!startExp) {
            throw std::runtime_error("Failed to run segmenter: " + startExp.error().message());
        }
        const auto result = m_segmenter->result().as<srt::driver::onnx::SessionResult>();
        if (!result) {
            throw std::runtime_error("Could not get segmenter session result");
        }

        auto boundaryIt = result->outputs.find("boundaries");
        if (boundaryIt == result->outputs.end()) {
            throw std::runtime_error("Missing boundaries output from segmenter");
        }

        const auto &boundaryTensor = boundaryIt->second;
        if (boundaryTensor->dataType() != srt::core::tensor_traits<bool>::data_type) {
            throw std::runtime_error("boundaries output has wrong data type");
        }
        const auto boundaryData = boundaryTensor->rawView();
        if (boundaryData.empty()) {
            throw std::runtime_error("Could not get boundaries data");
        }

        currentBoundaries.clear();
        currentBoundaries.reserve(boundaryData.size());
        for (size_t i = 0; i < boundaryData.size(); ++i) {
            currentBoundaries.push_back(boundaryData[i] != std::byte{0} ? 1 : 0);
        }
        if (currentBoundaries.size() != static_cast<size_t>(T)) {
            currentBoundaries.resize(static_cast<size_t>(T), 0);
        }
    }

    return currentBoundaries;
}

std::tuple<std::vector<float>, std::vector<uint8_t>>
GameExtractor::runBd2dur(const std::vector<uint8_t> &boundaries,
                         const std::vector<uint8_t> &maskT) const {
    if (boundaries.empty() || maskT.empty() || !m_bd2dur) {
        return {{}, {}};
    }

    const std::vector<int64_t> boundariesShape = {1, static_cast<int64_t>(boundaries.size())};
    const std::vector<int64_t> maskTShape = {1, static_cast<int64_t>(maskT.size())};

    const auto sessionInput = srt::core::NO<srt::driver::onnx::SessionStartInput>::create();

    // boundaries
    std::vector<bool> boundariesVec(boundaries.begin(), boundaries.end());
    auto boundariesExp = createBoolTensor(boundariesShape, boundariesVec);
    if (!boundariesExp) {
        throw std::runtime_error("Failed to create boundaries tensor: " + boundariesExp.error().message());
    }
    sessionInput->inputs["boundaries"] = boundariesExp.take();

    // maskT
    std::vector<bool> maskTVec(maskT.begin(), maskT.end());
    auto maskTExp = createBoolTensor(maskTShape, maskTVec);
    if (!maskTExp) {
        throw std::runtime_error("Failed to create maskT tensor: " + maskTExp.error().message());
    }
    sessionInput->inputs["maskT"] = maskTExp.take();

    sessionInput->outputs = {"durations", "maskN"};

    auto startExp = m_bd2dur->start(sessionInput);
    if (!startExp) {
        throw std::runtime_error("Failed to run bd2dur: " + startExp.error().message());
    }
    const auto result = m_bd2dur->result().as<srt::driver::onnx::SessionResult>();
    if (!result) {
        throw std::runtime_error("Could not get bd2dur session result");
    }

    // 提取 durations
    auto durIt = result->outputs.find("durations");
    if (durIt == result->outputs.end()) {
        throw std::runtime_error("Missing durations output from bd2dur");
    }
    const auto &durTensor = durIt->second;
    if (durTensor->dataType() != srt::core::tensor_traits<float>::data_type) {
        throw std::runtime_error("durations output has wrong data type");
    }
    const auto durData = durTensor->view<float>();
    if (durData.empty()) {
        throw std::runtime_error("Could not get durations data");
    }
    std::vector<float> durations = durData.vec();

    // 提取 maskN
    auto maskNIt = result->outputs.find("maskN");
    if (maskNIt == result->outputs.end()) {
        throw std::runtime_error("Missing maskN output from bd2dur");
    }
    const auto &maskNTensor = maskNIt->second;
    if (maskNTensor->dataType() != srt::core::tensor_traits<bool>::data_type) {
        throw std::runtime_error("maskN output has wrong data type");
    }
    const auto maskNData = maskNTensor->rawView();
    if (maskNData.empty()) {
        throw std::runtime_error("Could not get maskN data");
    }
    std::vector<uint8_t> maskN;
    maskN.reserve(maskNData.size());
    for (size_t i = 0; i < maskNData.size(); ++i) {
        maskN.push_back(maskNData[i] != std::byte{0} ? 1 : 0);
    }

    return {std::move(durations), std::move(maskN)};
}

std::tuple<std::vector<float>, std::vector<float>>
GameExtractor::runEstimator(const srt::core::NO<srt::core::ITensor> &xEst,
                            const std::vector<uint8_t> &boundaries,
                            const srt::core::NO<srt::core::ITensor> &maskT,
                            const std::vector<uint8_t> &maskN,
                            float threshold) const {
    if (boundaries.empty() || maskN.empty() || !m_estimator) {
        return {{}, {}};
    }

    if (!maskT) {
        throw std::runtime_error("runEstimator: maskT is null");
    }
    auto maskTShape = maskT->shape();
    if (maskTShape.size() != 2 || maskTShape[0] != 1) {
        throw std::runtime_error("runEstimator: maskT shape unexpected");
    }
    int64_t T = maskTShape[1];
    if (T <= 0) return {{}, {}};

    // 提取 maskT 数据
    auto maskTData = maskT->rawView();
    std::vector<uint8_t> maskTVec;
    maskTVec.reserve(static_cast<size_t>(T));
    for (int64_t i = 0; i < T; ++i) {
        maskTVec.push_back(maskTData[static_cast<size_t>(i)] != std::byte{0} ? 1 : 0);
    }

    // 调整 boundaries 大小
    std::vector<uint8_t> boundariesAdjusted = boundaries;
    if (boundariesAdjusted.size() != static_cast<size_t>(T)) {
        boundariesAdjusted.resize(static_cast<size_t>(T), 0);
    }

    // 提取 xEst 数据
    if (!xEst) {
        throw std::runtime_error("runEstimator: xEst is null");
    }
    auto xEstShape = xEst->shape();
    if (xEstShape.size() < 2 || xEstShape[1] != T) {
        throw std::runtime_error("runEstimator: xEst shape mismatch with T");
    }
    auto xEstExp = extractFloatTensor({{"x_est", xEst}}, "x_est");
    if (!xEstExp) {
        throw std::runtime_error("Failed to extract xEst tensor data: " + xEstExp.error().message());
    }
    std::vector<float> xEstData = xEstExp.take();

    auto sessionInput = srt::core::NO<srt::driver::onnx::SessionStartInput>::create();

    // x_est
    auto xEstTensorExp = srt::core::Tensor::createFromView<float>(xEstShape, stdc::array_view<float>{xEstData});
    if (!xEstTensorExp) {
        throw std::runtime_error("Failed to create xEst tensor: " + xEstTensorExp.error().message());
    }
    sessionInput->inputs["x_est"] = xEstTensorExp.take();

    // boundaries
    std::vector<bool> bdVec(boundariesAdjusted.begin(), boundariesAdjusted.end());
    auto bdExp = createBoolTensor({1, static_cast<int64_t>(bdVec.size())}, bdVec);
    if (!bdExp) {
        throw std::runtime_error("Failed to create boundaries tensor: " + bdExp.error().message());
    }
    sessionInput->inputs["boundaries"] = bdExp.take();

    // maskT
    std::vector<bool> mtVec(maskTVec.begin(), maskTVec.end());
    auto mtExp = createBoolTensor({1, static_cast<int64_t>(mtVec.size())}, mtVec);
    if (!mtExp) {
        throw std::runtime_error("Failed to create maskT tensor: " + mtExp.error().message());
    }
    sessionInput->inputs["maskT"] = mtExp.take();

    // maskN
    std::vector<bool> mnVec(maskN.begin(), maskN.end());
    auto mnExp = createBoolTensor({1, static_cast<int64_t>(mnVec.size())}, mnVec);
    if (!mnExp) {
        throw std::runtime_error("Failed to create maskN tensor: " + mnExp.error().message());
    }
    sessionInput->inputs["maskN"] = mnExp.take();

    // threshold
    std::vector<float> threshVec = {threshold};
    auto threshExp = srt::core::Tensor::createFromView<float>({1}, stdc::array_view<float>{threshVec});
    if (!threshExp) {
        throw std::runtime_error("Failed to create threshold tensor: " + threshExp.error().message());
    }
    sessionInput->inputs["threshold"] = threshExp.take();

    sessionInput->outputs = {"presence", "scores"};

    auto startExp = m_estimator->start(sessionInput);
    if (!startExp) {
        throw std::runtime_error("Failed to run estimator: " + startExp.error().message());
    }
    const auto result = m_estimator->result().as<srt::driver::onnx::SessionResult>();
    if (!result) {
        throw std::runtime_error("Could not get estimator session result");
    }

    // 提取 presence（bool → float）
    auto presIt = result->outputs.find("presence");
    if (presIt == result->outputs.end()) {
        throw std::runtime_error("Missing presence output from estimator");
    }
    const auto &presTensor = presIt->second;
    if (presTensor->dataType() != srt::core::tensor_traits<bool>::data_type) {
        throw std::runtime_error("presence output has wrong data type");
    }
    const auto presData = presTensor->rawView();
    if (presData.empty()) {
        throw std::runtime_error("Could not get presence data");
    }
    std::vector<float> presence;
    presence.reserve(presData.size());
    for (size_t i = 0; i < presData.size(); ++i) {
        presence.push_back(presData[i] != std::byte{0} ? 1.0f : 0.0f);
    }

    // 提取 scores
    auto scoresIt = result->outputs.find("scores");
    if (scoresIt == result->outputs.end()) {
        throw std::runtime_error("Missing scores output from estimator");
    }
    const auto &scoresTensor = scoresIt->second;
    if (scoresTensor->dataType() != srt::core::tensor_traits<float>::data_type) {
        throw std::runtime_error("scores output has wrong data type");
    }
    const auto scoresData = scoresTensor->view<float>();
    if (scoresData.empty()) {
        throw std::runtime_error("Could not get scores data");
    }
    std::vector<float> scores = scoresData.vec();

    return {std::move(presence), std::move(scores)};
}

GameExtractor::InferenceOutput GameExtractor::inferSlice(
    const std::vector<float> &waveform, float duration, int language,
    float segThreshold, int segRadiusFrames,
    float estThreshold, const std::vector<float> &d3pmTs) const {

    InferenceOutput output;

    // 1. encoder
    auto [xSegVal, xEstVal, maskTVal] = runEncoder(waveform, duration);
    if (!xSegVal || !xEstVal || !maskTVal) {
        return output;
    }

    auto maskTShape = maskTVal->shape();
    int64_t T = (maskTShape.size() >= 2) ? maskTShape[1] : 0;
    if (T <= 0) {
        return output;
    }

    // 提取 maskT 为 uint8_t vector
    auto maskTData = maskTVal->rawView();
    std::vector<uint8_t> maskTBool(static_cast<size_t>(T));
    for (int64_t i = 0; i < T; ++i) {
        if (static_cast<size_t>(i) < maskTData.size()) {
            maskTBool[static_cast<size_t>(i)] = maskTData[static_cast<size_t>(i)] != std::byte{0} ? 1 : 0;
        } else {
            maskTBool[static_cast<size_t>(i)] = 0;
        }
    }

    // 2. segmenter
    std::vector<uint8_t> knownBoundaries(static_cast<size_t>(T), 0);
    std::vector<uint8_t> boundaries = runSegmenter(
        xSegVal, knownBoundaries, knownBoundaries, language,
        maskTVal, segThreshold, segRadiusFrames, d3pmTs);
    if (boundaries.empty()) {
        boundaries.resize(static_cast<size_t>(T), 0);
    }

    // 3. bd2dur
    auto [durations, maskN] = runBd2dur(boundaries, maskTBool);
    if (durations.empty() || maskN.empty()) {
        output.boundaries = boundaries;
        return output;
    }

    // 4. estimator
    auto [presence, scores] = runEstimator(xEstVal, boundaries, maskTVal, maskN, estThreshold);

    output.boundaries = std::move(boundaries);
    output.durations = std::move(durations);
    output.presence = std::move(presence);
    output.scores = std::move(scores);
    output.maskN = std::move(maskN);

    return output;
}

// ============================================================================
// 静态辅助方法（从 Game.cpp 迁移）
// ============================================================================

std::vector<float> GameExtractor::generateD3pmTs() {
    // 原 generate_d3pm_ts(0.0f, 8)
    const float t0 = 0.0f;
    const int nSteps = 8;
    std::vector<float> ts;
    if (nSteps <= 0) return ts;
    const float step = (1.0f - t0) / static_cast<float>(nSteps);
    for (int i = 0; i < nSteps; ++i) {
        ts.push_back(t0 + static_cast<float>(i) * step);
    }
    return ts;
}

std::vector<double> GameExtractor::cumulativeSum(const std::vector<float> &durations) {
    std::vector<double> cumsum(durations.size());
    if (durations.empty()) return cumsum;
    cumsum[0] = static_cast<double>(durations[0]);
    for (size_t i = 1; i < durations.size(); ++i) {
        cumsum[i] = static_cast<double>(durations[i]) + cumsum[i - 1];
    }
    return cumsum;
}

std::vector<int> GameExtractor::calculateNoteTicks(const std::vector<float> &noteDurations,
                                                    float tempo) {
    const std::vector<double> cumsum = cumulativeSum(noteDurations);

    std::vector<double> scaledTicksDouble(cumsum.size());
    for (size_t i = 0; i < cumsum.size(); ++i) {
        scaledTicksDouble[i] = cumsum[i] * static_cast<double>(tempo) * 480.0 / 60.0;
    }

    std::vector<int> noteTicks(scaledTicksDouble.size());
    if (!scaledTicksDouble.empty()) {
        noteTicks[0] = static_cast<int>(std::round(scaledTicksDouble[0]));
        for (size_t i = 1; i < scaledTicksDouble.size(); ++i) {
            const double tickDiff = scaledTicksDouble[i] - scaledTicksDouble[i - 1];
            noteTicks[i] = static_cast<int>(std::round(tickDiff));
        }
    }
    return noteTicks;
}

std::vector<srt::extract::MidiNote> GameExtractor::buildMidiNotes(
    int startTick, const std::vector<float> &durations,
    const std::vector<float> &presence, const std::vector<float> &scores,
    float tempo) {

    std::vector<srt::extract::MidiNote> midiData;
    int currentTick = startTick;

    const std::vector<int> noteTicks = calculateNoteTicks(durations, tempo);

    for (size_t i = 0; i < durations.size(); ++i) {
        if (i < presence.size() && presence[i] > 0.5f) {
            const int pitch = static_cast<int>(std::round(scores[i]));
            const int durationTicks = (i < noteTicks.size()) ? noteTicks[i] : 0;

            midiData.push_back(srt::extract::MidiNote{pitch, currentTick, durationTicks});
            currentTick += durationTicks;
        } else {
            if (i < noteTicks.size()) {
                currentTick += noteTicks[i];
            }
        }
    }

    return midiData;
}
