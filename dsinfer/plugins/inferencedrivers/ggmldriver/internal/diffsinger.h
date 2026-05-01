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

namespace ds {

struct Config {
    uint32_t hidden_size = 256;
    uint32_t enc_layers  = 4;
    uint32_t num_heads   = 2;
    uint32_t enc_ffn_kernel_size = 3;
    std::string ffn_act = "gelu";
    bool use_rope = true;
    bool rope_interleaved = false;
    uint32_t vocab_size = 100;
    uint32_t mel_bins = 128;
    bool use_mix_ln = false;
    std::vector<uint32_t> mix_ln_layers;
    bool use_stretch_embed = false;
    bool use_spk_id = false;
    uint32_t num_spk = 0;
    bool use_variance_scaling = false;
    bool use_energy_embed = false;
    bool use_breathiness_embed = false;
    bool use_voicing_embed = false;
    bool use_tension_embed = false;
    bool use_key_shift_embed = false;
    bool use_speed_embed = false;

    std::string diffusion_type = "reflow";
    uint32_t time_scale_factor = 1000;
    std::string sampling_algorithm = "euler";
    uint32_t sampling_steps = 20;
    float t_start_infer = 0.0f;
    bool  use_variable_depth = false;
    float max_depth = 1.0f;

    std::string backbone_type = "lynxnet2";
    uint32_t bb_num_channels = 768;
    uint32_t bb_num_layers   = 6;
    uint32_t bb_kernel_size  = 31;
    uint32_t bb_expansion_factor = 1;
    std::string bb_glu_type = "atanglu";
    bool bb_use_conditioner_cache = true;

    // Optional aux_decoder (ConvNeXt) — used for shallow-diffusion x_end.
    bool has_aux_decoder = false;
    uint32_t aux_num_channels = 512;
    uint32_t aux_num_layers = 6;
    uint32_t aux_kernel_size = 7;
};

struct Model {
    Config cfg;
    ggml_backend_t backend = nullptr;
    ggml_backend_buffer_t weights_buffer = nullptr;
    ggml_context * ctx_w = nullptr;
    std::unordered_map<std::string, ggml_tensor*> tensors;
    std::vector<float> spec_min;
    std::vector<float> spec_max;

    // Optional FS2 modules — present if the GGUF was converted from a model
    // that uses them. C++ side queries with `get(name, optional=true)` and
    // skips silently when missing.
    bool has_lang_embed   = false;
    bool has_variance     = false;     // any of breathiness/voicing/tension/energy
    bool has_key_shift    = false;
    bool has_speed        = false;
    bool has_spk_embed    = false;

    ~Model();
    bool load(const std::string & gguf_path);
    ggml_tensor * get(const std::string & name, bool optional = false) const;
};

// What the inference pipeline should produce.
enum class OutputMode {
    Mel,    // run full pipeline (encoder + diffusion) and return mel
    Cond,   // run encoder only and return [T, hidden] cond, for alignment debug
};

struct InferenceInputs {
    std::vector<int32_t> tokens;       // length L
    std::vector<int32_t> durations;    // length L, frames per token
    std::vector<int32_t> mel2ph;       // optional length T, 1-based phone index
    std::vector<float>   f0;           // length T (sum of durations) in Hz

    // optional per-token / per-frame inputs (empty => not used)
    std::vector<int32_t> languages;        // length L  (for lang_embed)
    std::vector<float>   breathiness;      // length T
    std::vector<float>   voicing;          // length T
    std::vector<float>   tension;          // length T
    std::vector<float>   energy;           // length T
    std::vector<float>   gender;           // length T  (key_shift_embed driver)
    std::vector<float>   velocity;         // length T  (speed_embed driver)

    OutputMode mode = OutputMode::Mel;
    int  sampling_steps_override = -1; // <0 => use config
    std::string algorithm_override;    // empty => from GGUF; "euler"|"midpoint"|"rk4"
    uint32_t seed = 1234;
    int spk_id = 0;

    // Shallow diffusion controls.
    float depth = 1.0f;                // ∈ [0, max_depth]; 1 means full diffusion
    std::vector<float> aux_mel;        // optional [T, mel_bins] aux decoder mel for x_end
                                       // (required when computed t_start > 0)
};

struct InferenceOutputs {
    int T = 0;
    int dim = 0;             // mel_bins for Mel mode, hidden_size for Cond
    std::vector<float> data; // T * dim, row major
};

bool run_inference(const Model & model,
                   const InferenceInputs & in,
                   InferenceOutputs & out);

} // namespace ds
