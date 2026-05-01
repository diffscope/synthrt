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

namespace dsp_pitch {

struct FlowConfig {
    std::string backbone_type;
    uint32_t repeat_bins = 0;
    uint32_t channels = 0;
    uint32_t layers = 0;
    uint32_t kernel_size = 0;
    std::string glu_type;
    bool use_conditioner_cache = false;
};

struct PitchFlowConfig : FlowConfig {
    float pitd_norm_min = -8.0f;
    float pitd_norm_max = 8.0f;
    float pitd_clip_min = -12.0f;
    float pitd_clip_max = 12.0f;
};

struct VarianceFlowConfig : FlowConfig {
    uint32_t total_repeat_bins = 0;
    float voicing_norm_min = -96.0f;
    float voicing_norm_max = -12.0f;
    float voicing_clip_min = -96.0f;
    float voicing_clip_max = 0.0f;
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
    bool use_melody_encoder = false;
    std::string diffusion_type;
    uint32_t time_scale_factor = 1000;
    std::string sampling_algorithm = "euler";
    uint32_t sampling_steps = 20;
    uint32_t vocab_size = 0;
    uint32_t num_spk = 0;
    uint32_t num_lang = 0;

    // Melody encoder
    uint32_t melody_hidden_size = 128;
    uint32_t melody_enc_layers = 6;
    uint32_t melody_num_heads = 2;
    uint32_t melody_ffn_kernel_size = 3;

    // Pitch flow
    PitchFlowConfig pitch;

    // Optional variance (voicing)
    bool predict_voicing = false;
    VarianceFlowConfig variance;
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

// --- Input/Output Structs ---

struct PitchInputs {
    int phones = 0;            // L
    int frames = 0;            // T (mel frames)
    int notes = 0;             // N (note count)
    int spk_id = -1;
    uint32_t seed = 1234;

    std::vector<int32_t> tokens;         // [L]
    std::vector<int32_t> ph2word;        // [L], 1-based
    std::vector<float>   ph_dur;         // [L], frame counts per phone
    std::vector<int32_t> mel2ph;         // [T], 1-based
    std::vector<int32_t> languages;      // [L], optional

    // Melody inputs
    std::vector<float>   note_midi;      // [N], MIDI pitch (-1 = pad)
    std::vector<int32_t> note_rest;      // [N], 0/1
    std::vector<int32_t> note_dur;       // [N], duration in frames
    std::vector<int32_t> mel2note;       // [T], 1-based note index

    // Base pitch for delta computation (from note_seq expansion)
    std::vector<float>   base_pitch;     // [T], MIDI semitones
};

struct PitchOutputs {
    int frames = 0;
    std::vector<float> f0_hz;            // [T]
    std::vector<float> pitch_midi;       // [T], semitones
    std::vector<float> voicing;          // [T], empty if not predicted
};

// --- Inference Functions ---

// Run the full pitch prediction pipeline:
// 1. FS2 encoder → condition
// 2. Melody encoder → melody condition (gathered by mel2note)
// 3. Compose pitch condition (fs2 + melody + retake_embed + delta_pitch_embed + spk)
// 4. Pitch reflow → pitch_delta [T]
// 5. Output: base_pitch + pitch_delta → f0_hz
// 6. (Optional) Variance condition + reflow → voicing [T]
bool run_pitch_inference(const Model & m, const PitchInputs & in, PitchOutputs & out);

} // namespace dsp_pitch
