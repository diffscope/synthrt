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

namespace nsv {

struct Config {
    uint32_t num_mels = 0;
    uint32_t upsample_initial_channel = 0;
    uint32_t num_upsamples = 0;
    uint32_t num_resblocks = 0;
    bool mini_nsf = false;
    float noise_sigma = 0.0f;
    uint32_t sampling_rate = 0;
    uint32_t hop_size = 0;
    std::vector<uint32_t> upsample_rates;
    std::vector<uint32_t> upsample_kernel_sizes;
    std::vector<uint32_t> resblock_kernel_sizes;
    std::vector<uint32_t> resblock_dilations;
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

struct InferenceInputs {
    int frames = 0;
    std::vector<float> mel; // [T, num_mels] row-major
    std::vector<float> f0;  // [T], Hz
    bool mel_base10 = false; // SingingVocoders training uses natural-log mel.
    float noise_scale = 1.0f;
    uint32_t seed = 1234;
};

struct InferenceOutputs {
    int samples = 0;
    std::vector<float> wav;
};

bool run_vocoder(const Model & model,
                 const InferenceInputs & in,
                 InferenceOutputs & out);

} // namespace nsv
