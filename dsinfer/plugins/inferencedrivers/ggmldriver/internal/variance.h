#pragma once

#include <ggml.h>
#include <ggml-alloc.h>
#include <ggml-backend.h>
#include <ggml-cpu.h>
#include <gguf.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace dsv {

struct VarianceTarget {
    std::string name;
    float norm_min = 0.0f;
    float norm_max = 0.0f;
    float clip_min = 0.0f;
    float clip_max = 0.0f;
};

struct FlowConfig {
    std::string backbone_type;
    uint32_t repeat_bins = 0;
    uint32_t total_repeat_bins = 0;
    uint32_t channels = 0;
    uint32_t layers = 0;
    uint32_t kernel_size = 0;
    uint32_t expansion_factor = 1;
    std::string glu_type;
    bool use_conditioner_cache = false;
};

struct Config {
    uint32_t hidden_size = 0;
    uint32_t enc_layers = 0;
    uint32_t num_heads = 0;
    uint32_t enc_ffn_kernel_size = 0;
    std::string ffn_act;
    bool use_rope = false;
    bool rope_interleaved = true;
    bool use_lang_id = false;
    bool use_spk_id = false;
    bool use_variance_scaling = false;
    std::string diffusion_type;
    uint32_t time_scale_factor = 1000;
    std::string sampling_algorithm = "euler";
    uint32_t sampling_steps = 20;
    uint32_t vocab_size = 0;
    uint32_t num_spk = 0;
    uint32_t num_lang = 0;
    bool predict_dur = false;
    bool predict_energy = false;
    bool predict_breathiness = false;
    bool predict_voicing = false;
    bool predict_tension = false;
    FlowConfig variance;
    std::vector<VarianceTarget> variance_targets;
};

struct Model {
    Config cfg;
    ggml_backend_t backend = nullptr;
    ggml_backend_buffer_t weights_buffer = nullptr;
    ggml_context * ctx_w = nullptr;
    std::unordered_map<std::string, ggml_tensor *> tensors;

    ~Model();
    bool load(const std::string & gguf_path);
    ggml_tensor * get(const std::string & name, bool optional = false) const;
};

struct VarianceConditionInputs {
    int frames = 0;
    std::vector<float> fs2_condition; // [T, hidden_size] row-major
    std::vector<float> pitch;         // [T], semitones
};

struct VarianceSampleInputs {
    int frames = 0;
    uint32_t seed = 1234;
    std::vector<float> condition; // [T, hidden_size] row-major
};

struct VarianceSampleOutputs {
    int frames = 0;
    std::vector<VarianceTarget> targets;
    std::vector<std::vector<float>> values; // one [T] curve per target
};

struct FS2ConditionInputs {
    int phones = 0;
    int frames = 0;
    int spk_id = -1;
    std::vector<int32_t> tokens;    // [L]
    std::vector<int32_t> ph2word;   // [L], 1-based, word-mode variance encoder
    std::vector<float> word_dur;    // [W], frames, word-mode variance encoder
    std::vector<int32_t> languages; // [L], optional
    std::vector<int32_t> mel2ph;    // [T], 1-based, 0 means pad
};

struct ConditionOutputs {
    int frames = 0;
    int hidden = 0;
    std::vector<float> condition; // [T, hidden_size] row-major
};

bool run_fs2_condition(const Model & model,
                       const FS2ConditionInputs & in,
                       ConditionOutputs & out);

bool compose_variance_condition(const Model & model,
                                const VarianceConditionInputs & in,
                                ConditionOutputs & out);

bool sample_variances(const Model & model,
                      const VarianceSampleInputs & in,
                      VarianceSampleOutputs & out);

} // namespace dsv
