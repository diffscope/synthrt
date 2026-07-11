#include "RmvpeExtractor.h"

#include <cmath>
#include <map>
#include <set>
#include <vector>

#include <stdcorelib/str.h>

#include <synthrt/Audio/Slicer.h>
#include <synthrt/Core/Support/Diagnostic.h>
#include <synthrt/Core/Tensor/Tensor.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <synthrt/Extract/AudioPreprocessor.h>
#include <synthrt/Extract/ExtractorDriver.h>

RmvpeExtractor::RmvpeExtractor(srt::core::Runtime *runtime)
    : m_runtime(runtime) {
}

srt::core::Expected<void>
RmvpeExtractor::open(const std::filesystem::path &modelPath) {
    if (!m_driver) {
        if (auto exp = srt::extract::getInferenceDriver(m_runtime); !exp) {
            return exp.takeError();
        } else {
            m_driver = exp.take();
        }
    }

    auto session = m_driver->createSession();
    if (!session) {
        return srt::core::Error(
            srt::core::ErrorCode::ExtractModelOpenFailed,
            "open: could not create ONNX session");
    }
    auto openArgs = srt::core::NO<srt::driver::onnx::SessionOpenArgs>::create();
    if (auto exp = session->open(modelPath, openArgs); !exp) {
        return srt::core::Error(
            srt::core::ErrorCode::ExtractModelOpenFailed,
            stdc::formatN("open: failed to open RMVPE model '%1': %2",
                          modelPath.string(), exp.error().message()));
    }
    m_session = std::move(session);
    return srt::core::Expected<void>();
}

bool RmvpeExtractor::isOpen() const {
    return m_session != nullptr;
}

void RmvpeExtractor::close() {
    if (m_session) {
        m_session->stop();
        m_session->close();
        m_session.reset();
    }
}

void RmvpeExtractor::terminate() {
    if (m_session) {
        m_session->stop();
    }
}

srt::extract::AudioRequirements RmvpeExtractor::audioRequirements() const {
    return {16000, 1};  // RMVPE requires 16000 Hz mono
}

srt::core::Expected<srt::extract::PitchResult>
RmvpeExtractor::extract(const srt::audio::AudioBuffer &buffer,
                        int sampleRate,
                        const srt::extract::ProgressCallback &progress) {
    if (!isOpen()) {
        return srt::core::Error(
            srt::core::ErrorCode::ExtractNotInitialized,
            "extract: RMVPE session is not open");
    }

    // 1. Resample to 16000 Hz mono and slice by RMS.
    //    Slicer params migrated from Rmvpe.cpp:117; the first argument is
    //    corrected from 160 (hopSize) to 16000 (sampleRate).
    const auto req = audioRequirements();
    srt::audio::Slicer slicer(16000, 0.02f, 160, 160 * 4, 500, 30, 50);
    auto slicesExp = srt::extract::AudioPreprocessor::prepare(
        buffer, sampleRate, req, slicer);
    if (!slicesExp) {
        return slicesExp.takeError();
    }
    const auto &slices = slicesExp.take();

    if (slices.empty()) {
        return srt::core::Error(
            srt::core::ErrorCode::ExtractOutputInvalid,
            "extract: slicer produced no audio chunks");
    }

    // 2. Per-slice forward inference.
    srt::extract::PitchResult result;
    constexpr float threshold = 0.03f;

    int64_t totalFrames = 0;
    for (const auto &slice : slices) {
        totalFrames += slice.endFrame - slice.startFrame;
    }

    int64_t processedFrames = 0;
    for (const auto &slice : slices) {
        srt::extract::PitchFrame frame;
        frame.offset = static_cast<float>(
            static_cast<double>(slice.startFrame) / (16000.0 / 1000.0));

        std::vector<float> f0;
        std::vector<bool> uv;
        if (auto exp = forward(slice.samples, threshold, f0, uv); !exp) {
            return exp.takeError();
        }
        interpF0(f0, uv);
        frame.f0 = std::move(f0);
        frame.uv = std::move(uv);
        result.frames.push_back(std::move(frame));

        processedFrames += slice.endFrame - slice.startFrame;
        if (progress && totalFrames > 0) {
            progress(static_cast<int>(
                static_cast<float>(processedFrames) / totalFrames * 100));
        }
    }
    return result;
}

srt::core::Expected<void>
RmvpeExtractor::forward(const std::vector<float> &waveform, float threshold,
                        std::vector<float> &f0, std::vector<bool> &uv) {
    if (!m_session) {
        return srt::core::Error(
            srt::core::ErrorCode::ExtractNotInitialized,
            "forward: RMVPE session is not initialized");
    }

    const auto n = waveform.size();
    const std::vector<int64_t> waveformShape = {1, static_cast<int64_t>(n)};
    auto sessionInput = srt::core::NO<srt::driver::onnx::SessionStartInput>::create();

    if (auto exp = srt::core::Tensor::createFromView<float>(waveformShape, waveform); !exp) {
        return exp.takeError();
    } else {
        sessionInput->inputs["waveform"] = exp.take();
    }

    if (auto exp = srt::core::Tensor::createScalar<float>(threshold); !exp) {
        return exp.takeError();
    } else {
        sessionInput->inputs["threshold"] = exp.take();
    }

    sessionInput->outputs = {"f0", "uv"};

    auto sessionExp = m_session->start(sessionInput);
    if (!sessionExp) {
        return srt::core::Error(
            srt::core::ErrorCode::ExtractInferenceFailed,
            stdc::formatN("forward: RMVPE session start failed: %1",
                          sessionExp.error().message()));
    }

    auto sessionTaskResult = sessionExp.take();
    if (!sessionTaskResult) {
        return srt::core::Error(
            srt::core::ErrorCode::ExtractInferenceFailed,
            "forward: could not get RMVPE session result");
    }
    auto sessionResult = sessionTaskResult.as<srt::driver::onnx::SessionResult>();
    if (!sessionResult) {
        return srt::core::Error(
            srt::core::ErrorCode::ExtractInferenceFailed,
            "forward: invalid RMVPE session result type");
    }

    // Extract f0 (float) output.
    if (auto it = sessionResult->outputs.find("f0");
        it != sessionResult->outputs.end()) {
        const auto &tensor = it->second;
        if (tensor->dataType() != srt::core::ITensor::Float) {
            return srt::core::Error(
                srt::core::ErrorCode::ExtractOutputInvalid,
                "forward: f0 output data type mismatch (expected Float)");
        }
        const auto view = tensor->view<float>();
        if (view.empty()) {
            return srt::core::Error(
                srt::core::ErrorCode::ExtractOutputInvalid,
                "forward: f0 output is empty");
        }
        f0.assign(view.begin(), view.end());
    } else {
        return srt::core::Error(
            srt::core::ErrorCode::ExtractOutputInvalid,
            "forward: missing output 'f0'");
    }

    // Extract uv (bool) output.
    if (auto it = sessionResult->outputs.find("uv");
        it != sessionResult->outputs.end()) {
        const auto &tensor = it->second;
        if (tensor->dataType() != srt::core::ITensor::Bool) {
            return srt::core::Error(
                srt::core::ErrorCode::ExtractOutputInvalid,
                "forward: uv output data type mismatch (expected Bool)");
        }
        const auto raw = tensor->rawView();
        if (raw.empty()) {
            return srt::core::Error(
                srt::core::ErrorCode::ExtractOutputInvalid,
                "forward: uv output is empty");
        }
        uv.resize(raw.size());
        for (size_t i = 0; i < raw.size(); ++i) {
            uv[i] = raw[i] != std::byte{0};
        }
    } else {
        return srt::core::Error(
            srt::core::ErrorCode::ExtractOutputInvalid,
            "forward: missing output 'uv'");
    }

    return srt::core::Expected<void>();
}

void RmvpeExtractor::interpF0(std::vector<float> &f0, std::vector<bool> &uv) {
    const int n = static_cast<int>(f0.size());

    // Edge case: no unvoiced frames, no interpolation needed.
    bool allVoiced = true;
    for (int i = 0; i < n; ++i) {
        if (!uv[i]) {
            allVoiced = false;
            break;
        }
    }
    if (allVoiced) {
        return;
    }

    int firstUnvoiced = -1;
    int lastUnvoiced = -1;

    for (int i = 0; i < n; ++i) {
        if (!uv[i]) {
            firstUnvoiced = i;
            break;
        }
    }
    for (int i = n - 1; i >= 0; --i) {
        if (!uv[i]) {
            lastUnvoiced = i;
            break;
        }
    }

    if (firstUnvoiced == -1 || lastUnvoiced == -1) {
        return;
    }

    // Fill frames before the first unvoiced frame with its value.
    for (int i = 0; i < firstUnvoiced; ++i) {
        f0[i] = f0[firstUnvoiced];
    }
    // Fill frames after the last unvoiced frame with its value.
    for (int i = n - 1; i > lastUnvoiced; --i) {
        f0[i] = f0[lastUnvoiced];
    }

    // Interpolate voiced gaps between the first and last unvoiced frames.
    for (int i = firstUnvoiced; i < lastUnvoiced; ++i) {
        if (uv[i]) {
            const int prev = i - 1;
            int next = i + 1;
            while (next < n && uv[next]) {
                ++next;
            }
            if (next < n) {
                const float ratio = std::log(f0[next] / f0[prev]);
                f0[i] = static_cast<float>(
                    f0[prev] * std::exp(ratio * static_cast<long double>(i - prev) /
                                        (next - prev)));
            }
        }
    }
}
