#include "Session.h"

#include <stdcorelib/pimpl.h>

#include <ggml.h>
#include <gguf.h>

#include <dsinfer/Core/Tensor.h>

#include "GgmlDriver_Logger.h"

#include "diffsinger.h"
#include "variance.h"
#include "pitch.h"
#include "vocoder.h"

namespace fs = std::filesystem;

namespace ds::ggmldriver {

    using ggmldriver::Log;

    enum class ModelType {
        Unknown,
        Acoustic,
        Variance,
        Pitch,
        Vocoder,
    };

    static ModelType detectModelType(const std::string &path) {
        gguf_init_params gp{/*no_alloc=*/true, /*ctx=*/nullptr};
        gguf_context *gctx = gguf_init_from_file(path.c_str(), gp);
        if (!gctx) {
            return ModelType::Unknown;
        }

        std::string arch;
        int64_t id = gguf_find_key(gctx, "general.architecture");
        if (id >= 0) {
            arch = gguf_get_val_str(gctx, id);
        }
        gguf_free(gctx);

        if (arch == "diffsinger-acoustic")
            return ModelType::Acoustic;
        if (arch == "diffsinger-variance")
            return ModelType::Variance;
        if (arch == "diffsinger-pitch")
            return ModelType::Pitch;
        if (arch == "nsf-hifigan")
            return ModelType::Vocoder;

        // Fallback: check for model-specific metadata keys
        return ModelType::Unknown;
    }

    class Session::Impl {
    public:
        bool opened = false;
        fs::path modelPath;
        ModelType modelType = ModelType::Unknown;
        srt::NO<Api::Common::SessionResult> sessionResult;

        // One of these will be populated based on modelType
        std::unique_ptr<ds::Model> acousticModel;
        std::unique_ptr<dsv::Model> varianceModel;
        std::unique_ptr<dsp_pitch::Model> pitchModel;
        std::unique_ptr<nsv::Model> vocoderModel;

        Impl() : sessionResult(srt::NO<Api::Common::SessionResult>::create()) {
        }

        srt::Expected<srt::NO<Api::Common::SessionResult>>
        runAcoustic(const srt::NO<Api::Common::SessionStartInput> &input);

        srt::Expected<srt::NO<Api::Common::SessionResult>>
        runVariance(const srt::NO<Api::Common::SessionStartInput> &input);

        srt::Expected<srt::NO<Api::Common::SessionResult>>
        runPitch(const srt::NO<Api::Common::SessionStartInput> &input);

        srt::Expected<srt::NO<Api::Common::SessionResult>>
        runVocoder(const srt::NO<Api::Common::SessionStartInput> &input);
    };

    // --- Helper: extract tensor data as vector<T> ---

    template <typename T>
    static std::vector<T> tensorToVec(const srt::NO<ITensor> &t) {
        if (!t)
            return {};
        auto count = t->elementCount();
        std::vector<T> v(count);
        if (auto p = t->data<T>()) {
            std::copy(p, p + count, v.begin());
        } else if constexpr (std::is_same_v<T, int32_t>) {
            // Convert from Int64 if needed
            if (auto p64 = t->data<int64_t>()) {
                for (size_t i = 0; i < count; ++i)
                    v[i] = static_cast<int32_t>(p64[i]);
            }
        } else if constexpr (std::is_same_v<T, float>) {
            // Already float, data() should have worked
        }
        return v;
    }

    static int64_t tensorToScalarI64(const srt::NO<ITensor> &t, int64_t def = 0) {
        if (!t)
            return def;
        if (auto p = t->data<int64_t>())
            return p[0];
        if (auto p = t->data<int32_t>())
            return static_cast<int64_t>(p[0]);
        if (auto p = t->data<float>())
            return static_cast<int64_t>(p[0]);
        return def;
    }

    static float tensorToScalarF32(const srt::NO<ITensor> &t, float def = 0.0f) {
        if (!t)
            return def;
        if (auto p = t->data<float>())
            return p[0];
        if (auto p = t->data<int64_t>())
            return static_cast<float>(p[0]);
        return def;
    }

    // --- Acoustic inference ---
    srt::Expected<srt::NO<Api::Common::SessionResult>>
    Session::Impl::runAcoustic(const srt::NO<Api::Common::SessionStartInput> &input) {
        auto &m = *acousticModel;
        ds::InferenceInputs in;
        ds::InferenceOutputs out;

        auto find = [&](const std::string &name) -> srt::NO<ITensor> {
            auto it = input->inputs.find(name);
            return it != input->inputs.end() ? it->second : nullptr;
        };

        in.tokens = tensorToVec<int32_t>(find("tokens"));
        in.f0 = tensorToVec<float>(find("f0"));
        in.breathiness = tensorToVec<float>(find("breathiness"));
        in.voicing = tensorToVec<float>(find("voicing"));
        in.tension = tensorToVec<float>(find("tension"));
        in.energy = tensorToVec<float>(find("energy"));
        in.gender = tensorToVec<float>(find("gender"));
        in.velocity = tensorToVec<float>(find("velocity"));
        in.languages = tensorToVec<int32_t>(find("languages"));

        // Durations
        auto durTensor = find("durations");
        if (durTensor) {
            auto durVec = tensorToVec<int64_t>(durTensor);
            in.durations.resize(durVec.size());
            for (size_t i = 0; i < durVec.size(); ++i)
                in.durations[i] = static_cast<int32_t>(durVec[i]);
        }

        // Compute mel2ph from durations if not provided
        if (in.mel2ph.empty() && !in.durations.empty()) {
            int T = 0;
            for (auto d : in.durations)
                T += d;
            in.mel2ph.resize(T);
            int idx = 0;
            for (int p = 0; p < (int)in.durations.size(); ++p) {
                for (int j = 0; j < in.durations[p]; ++j) {
                    in.mel2ph[idx++] = p + 1; // 1-based
                }
            }
        }

        // Steps / speedup
        auto stepsTensor = find("steps");
        auto speedupTensor = find("speedup");
        if (stepsTensor) {
            in.sampling_steps_override = static_cast<int>(tensorToScalarI64(stepsTensor));
        } else if (speedupTensor) {
            int64_t speedup = tensorToScalarI64(speedupTensor, 1);
            if (speedup > 0 && m.cfg.sampling_steps > 0) {
                in.sampling_steps_override =
                    static_cast<int>(m.cfg.sampling_steps / static_cast<uint32_t>(speedup));
            }
        }

        // Depth
        auto depthTensor = find("depth");
        if (depthTensor) {
            if (m.cfg.use_variable_depth) {
                in.depth = tensorToScalarF32(depthTensor, 1.0f);
            } else {
                int64_t intDepth = tensorToScalarI64(depthTensor, 1000);
                in.depth = static_cast<float>(intDepth) / 1000.0f;
            }
        }

        // Speaker
        auto spkTensor = find("spk_embed");
        if (spkTensor) {
            in.spk_id = static_cast<int>(tensorToScalarI64(spkTensor, 0));
        }

        in.mode = ds::OutputMode::Mel;

        if (!ds::run_inference(m, in, out)) {
            return srt::Error(srt::Error::SessionError, "acoustic inference failed");
        }

        // Wrap output mel as ITensor [T, mel_bins]
        auto result = srt::NO<Api::Common::SessionResult>::create();
        std::vector<int64_t> melShape = {static_cast<int64_t>(out.T), static_cast<int64_t>(out.dim)};
        stdc::array_view<float> melView(out.data.data(), out.data.size());
        auto melTensor = Tensor::createFromView(melShape, melView);
        if (melTensor) {
            result->outputs["mel"] = melTensor.take();
        }
        return result;
    }

    // --- Vocoder inference ---
    srt::Expected<srt::NO<Api::Common::SessionResult>>
    Session::Impl::runVocoder(const srt::NO<Api::Common::SessionStartInput> &input) {
        auto &m = *vocoderModel;
        nsv::InferenceInputs in;
        nsv::InferenceOutputs out;

        auto find = [&](const std::string &name) -> srt::NO<ITensor> {
            auto it = input->inputs.find(name);
            return it != input->inputs.end() ? it->second : nullptr;
        };

        in.mel = tensorToVec<float>(find("mel"));
        in.f0 = tensorToVec<float>(find("f0"));
        in.frames = static_cast<int>(in.f0.size());

        if (!nsv::run_vocoder(m, in, out)) {
            return srt::Error(srt::Error::SessionError, "vocoder inference failed");
        }

        auto result = srt::NO<Api::Common::SessionResult>::create();
        std::vector<int64_t> wavShape = {static_cast<int64_t>(out.samples)};
        stdc::array_view<float> wavView(out.wav.data(), out.wav.size());
        auto wavTensor = Tensor::createFromView(wavShape, wavView);
        if (wavTensor) {
            result->outputs["waveform"] = wavTensor.take();
        }
        return result;
    }

    // --- Variance inference ---
    srt::Expected<srt::NO<Api::Common::SessionResult>>
    Session::Impl::runVariance(const srt::NO<Api::Common::SessionStartInput> &input) {
        auto &m = *varianceModel;

        auto find = [&](const std::string &name) -> srt::NO<ITensor> {
            auto it = input->inputs.find(name);
            return it != input->inputs.end() ? it->second : nullptr;
        };

        // FS2 condition
        dsv::FS2ConditionInputs fs2In;
        fs2In.tokens = tensorToVec<int32_t>(find("tokens"));
        fs2In.phones = static_cast<int>(fs2In.tokens.size());
        fs2In.languages = tensorToVec<int32_t>(find("languages"));

        auto spkTensor = find("spk_embed");
        if (spkTensor) {
            fs2In.spk_id = static_cast<int>(tensorToScalarI64(spkTensor, -1));
        }

        // Durations
        auto durTensor = find("durations");
        if (durTensor) {
            auto durVec = tensorToVec<int64_t>(durTensor);
            // Compute mel2ph and frames
            int T = 0;
            for (auto d : durVec) T += static_cast<int>(d);
            fs2In.frames = T;
            fs2In.mel2ph.resize(T);
            int idx = 0;
            for (int p = 0; p < (int)durVec.size(); ++p) {
                for (int j = 0; j < (int)durVec[p]; ++j) {
                    fs2In.mel2ph[idx++] = p + 1;
                }
            }
        }

        dsv::ConditionOutputs condOut;
        if (!dsv::run_fs2_condition(m, fs2In, condOut)) {
            return srt::Error(srt::Error::SessionError, "variance FS2 condition failed");
        }

        // Variance condition + sampling
        dsv::VarianceConditionInputs varCondIn;
        varCondIn.frames = condOut.frames;
        varCondIn.fs2_condition = condOut.condition;

        // Get pitch for condition
        auto f0Tensor = find("f0");
        if (f0Tensor) {
            varCondIn.pitch = tensorToVec<float>(f0Tensor);
        }

        dsv::ConditionOutputs varCondOut;
        if (!dsv::compose_variance_condition(m, varCondIn, varCondOut)) {
            return srt::Error(srt::Error::SessionError, "variance condition failed");
        }

        dsv::VarianceSampleInputs sampleIn;
        sampleIn.frames = varCondOut.frames;
        sampleIn.condition = varCondOut.condition;

        dsv::VarianceSampleOutputs sampleOut;
        if (!dsv::sample_variances(m, sampleIn, sampleOut)) {
            return srt::Error(srt::Error::SessionError, "variance sampling failed");
        }

        auto result = srt::NO<Api::Common::SessionResult>::create();
        for (size_t i = 0; i < sampleOut.targets.size(); ++i) {
            const auto &target = sampleOut.targets[i];
            const auto &values = sampleOut.values[i];
            std::vector<int64_t> shape = {static_cast<int64_t>(values.size())};
            stdc::array_view<float> view(values.data(), values.size());
            auto tensor = Tensor::createFromView(shape, view);
            if (tensor) {
                result->outputs[target.name] = tensor.take();
            }
        }
        return result;
    }

    // --- Pitch inference ---
    srt::Expected<srt::NO<Api::Common::SessionResult>>
    Session::Impl::runPitch(const srt::NO<Api::Common::SessionStartInput> &input) {
        auto &m = *pitchModel;
        dsp_pitch::PitchInputs in;
        dsp_pitch::PitchOutputs out;

        auto find = [&](const std::string &name) -> srt::NO<ITensor> {
            auto it = input->inputs.find(name);
            return it != input->inputs.end() ? it->second : nullptr;
        };

        in.tokens = tensorToVec<int32_t>(find("tokens"));
        in.phones = static_cast<int>(in.tokens.size());
        in.languages = tensorToVec<int32_t>(find("languages"));
        in.ph2word = tensorToVec<int32_t>(find("ph2word"));
        in.note_midi = tensorToVec<float>(find("note_midi"));
        in.note_rest = tensorToVec<int32_t>(find("note_rest"));
        in.note_dur = tensorToVec<int32_t>(find("note_dur"));
        in.notes = static_cast<int>(in.note_midi.size());
        in.mel2note = tensorToVec<int32_t>(find("mel2note"));
        in.base_pitch = tensorToVec<float>(find("base_pitch"));
        in.mel2ph = tensorToVec<int32_t>(find("mel2ph"));
        in.ph_dur = tensorToVec<float>(find("ph_dur"));

        // Compute frames from mel2ph
        if (!in.mel2ph.empty()) {
            in.frames = static_cast<int>(in.mel2ph.size());
        }

        auto spkTensor = find("spk_embed");
        if (spkTensor) {
            in.spk_id = static_cast<int>(tensorToScalarI64(spkTensor, -1));
        }

        if (!dsp_pitch::run_pitch_inference(m, in, out)) {
            return srt::Error(srt::Error::SessionError, "pitch inference failed");
        }

        auto result = srt::NO<Api::Common::SessionResult>::create();

        std::vector<int64_t> shape = {static_cast<int64_t>(out.frames)};
        stdc::array_view<float> f0View(out.f0_hz.data(), out.f0_hz.size());
        auto f0Tensor = Tensor::createFromView(shape, f0View);
        if (f0Tensor) {
            result->outputs["f0"] = f0Tensor.take();
        }

        if (!out.voicing.empty()) {
            stdc::array_view<float> voicingView(out.voicing.data(), out.voicing.size());
            auto voicingTensor = Tensor::createFromView(shape, voicingView);
            if (voicingTensor) {
                result->outputs["voicing"] = voicingTensor.take();
            }
        }

        return result;
    }

    // --- Session lifecycle ---

    Session::Session() : _impl(std::make_unique<Impl>()) {
    }

    Session::~Session() {
        close();
    }

    srt::Expected<void> Session::open(const fs::path &path,
                                      const srt::NO<Api::Common::SessionOpenArgs> &args) {
        __stdc_impl_t;

        if (isOpen()) {
            return srt::Error(srt::Error::SessionError, "session is already open");
        }

        if (!fs::is_regular_file(path)) {
            return srt::Error(srt::Error::FileNotOpen, "not a regular file");
        }

        fs::path canonical_path = fs::canonical(path);
        std::string pathStr = canonical_path.string();
        Log.srtInfo("Session - Opening GGUF model: %1", pathStr);

        impl.modelType = detectModelType(pathStr);
        if (impl.modelType == ModelType::Unknown) {
            return srt::Error(srt::Error::FileNotOpen,
                              "unrecognized GGUF architecture in " + pathStr);
        }

        bool ok = false;
        switch (impl.modelType) {
            case ModelType::Acoustic: {
                auto model = std::make_unique<ds::Model>();
                ok = model->load(pathStr);
                if (ok)
                    impl.acousticModel = std::move(model);
                break;
            }
            case ModelType::Variance: {
                auto model = std::make_unique<dsv::Model>();
                ok = model->load(pathStr);
                if (ok)
                    impl.varianceModel = std::move(model);
                break;
            }
            case ModelType::Pitch: {
                auto model = std::make_unique<dsp_pitch::Model>();
                ok = model->load(pathStr);
                if (ok)
                    impl.pitchModel = std::move(model);
                break;
            }
            case ModelType::Vocoder: {
                auto model = std::make_unique<nsv::Model>();
                ok = model->load(pathStr);
                if (ok)
                    impl.vocoderModel = std::move(model);
                break;
            }
            default:
                break;
        }

        if (!ok) {
            return srt::Error(srt::Error::FileNotOpen, "failed to load GGUF model: " + pathStr);
        }

        impl.modelPath = canonical_path;
        impl.opened = true;
        Log.srtInfo("Session - Model loaded successfully");
        return srt::Expected<void>();
    }

    srt::Expected<void> Session::close() {
        __stdc_impl_t;

        if (!impl.opened) {
            return srt::Error(srt::Error::SessionError, "session is not open");
        }

        impl.acousticModel.reset();
        impl.varianceModel.reset();
        impl.pitchModel.reset();
        impl.vocoderModel.reset();
        impl.modelType = ModelType::Unknown;
        impl.opened = false;
        impl.modelPath.clear();

        return srt::Expected<void>();
    }

    bool Session::isOpen() const {
        __stdc_impl_t;
        return impl.opened;
    }

    srt::Expected<srt::NO<srt::TaskResult>> Session::run(const srt::NO<srt::TaskStartInput> &input) {
        __stdc_impl_t;

        if (!impl.opened) {
            return srt::Error(srt::Error::SessionError, "session is not open");
        }

        auto startInput = input.as<Api::Common::SessionStartInput>();
        if (!startInput) {
            return srt::Error(srt::Error::InvalidArgument, "invalid task start input");
        }

        srt::Expected<srt::NO<Api::Common::SessionResult>> resultExp;

        switch (impl.modelType) {
            case ModelType::Acoustic:
                resultExp = impl.runAcoustic(startInput);
                break;
            case ModelType::Variance:
                resultExp = impl.runVariance(startInput);
                break;
            case ModelType::Pitch:
                resultExp = impl.runPitch(startInput);
                break;
            case ModelType::Vocoder:
                resultExp = impl.runVocoder(startInput);
                break;
            default:
                return srt::Error(srt::Error::SessionError, "unknown model type");
        }

        if (!resultExp) {
            return resultExp.takeError();
        }
        impl.sessionResult = resultExp.take();
        return impl.sessionResult;
    }

    srt::NO<srt::TaskResult> Session::result() const {
        __stdc_impl_t;
        return impl.sessionResult.as<srt::TaskResult>();
    }

}
