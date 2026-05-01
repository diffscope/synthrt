#include "vocoder.h"
#include "runtime_backend.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace nsv {

Model::~Model() {
    if (weights_buffer) ggml_backend_buffer_free(weights_buffer);
    if (ctx_w)          ggml_free(ctx_w);
    if (backend)        ggml_backend_free(backend);
}

ggml_tensor * Model::get(const std::string & name, bool optional) const {
    auto it = tensors.find(name);
    if (it == tensors.end()) {
        if (optional) return nullptr;
        fprintf(stderr, "[fatal] missing tensor '%s' in GGUF\n", name.c_str());
        std::abort();
    }
    return it->second;
}

static std::string get_str(gguf_context * gc, const char * key, const char * def = "") {
    int64_t id = gguf_find_key(gc, key);
    return id < 0 ? std::string(def) : std::string(gguf_get_val_str(gc, id));
}

static uint32_t get_u32(gguf_context * gc, const char * key, uint32_t def = 0) {
    int64_t id = gguf_find_key(gc, key);
    return id < 0 ? def : gguf_get_val_u32(gc, id);
}

static bool get_bool(gguf_context * gc, const char * key, bool def = false) {
    int64_t id = gguf_find_key(gc, key);
    return id < 0 ? def : gguf_get_val_bool(gc, id);
}

static float get_f32(gguf_context * gc, const char * key, float def = 0.0f) {
    int64_t id = gguf_find_key(gc, key);
    return id < 0 ? def : gguf_get_val_f32(gc, id);
}

bool Model::load(const std::string & path) {
    backend = dsrt::init_backend("vocoder");
    if (!backend) return false;

    ggml_context * ctx_meta = nullptr;
    gguf_init_params gp { true, &ctx_meta };
    gguf_context * gctx = gguf_init_from_file(path.c_str(), gp);
    if (!gctx) {
        fprintf(stderr, "[fatal] gguf_init_from_file failed: %s\n", path.c_str());
        return false;
    }

    std::string arch = get_str(gctx, "general.architecture");
    if (arch != "nsf-hifigan") {
        fprintf(stderr, "[fatal] expected nsf-hifigan GGUF, got '%s'\n", arch.c_str());
        gguf_free(gctx);
        ggml_free(ctx_meta);
        return false;
    }

    cfg.num_mels = get_u32(gctx, "nsf-hifigan.num_mels", 0);
    cfg.upsample_initial_channel = get_u32(gctx, "nsf-hifigan.upsample_initial_channel", 0);
    cfg.num_upsamples = get_u32(gctx, "nsf-hifigan.num_upsamples", 0);
    cfg.num_resblocks = get_u32(gctx, "nsf-hifigan.num_resblocks", 0);
    cfg.mini_nsf = get_bool(gctx, "nsf-hifigan.mini_nsf", false);
    cfg.noise_sigma = get_f32(gctx, "nsf-hifigan.noise_sigma", 0.0f);
    cfg.sampling_rate = get_u32(gctx, "nsf-hifigan.sampling_rate", 0);
    cfg.hop_size = get_u32(gctx, "nsf-hifigan.hop_size", 0);
    for (uint32_t i = 0; i < cfg.num_upsamples; ++i) {
        char key[96];
        snprintf(key, sizeof key, "nsf-hifigan.upsample_rates.%u", i);
        cfg.upsample_rates.push_back(get_u32(gctx, key, 0));
        snprintf(key, sizeof key, "nsf-hifigan.upsample_kernel_sizes.%u", i);
        cfg.upsample_kernel_sizes.push_back(get_u32(gctx, key, 0));
    }
    for (uint32_t i = 0;; ++i) {
        char key[96];
        snprintf(key, sizeof key, "nsf-hifigan.resblock_kernel_sizes.%u", i);
        int64_t id = gguf_find_key(gctx, key);
        if (id < 0) break;
        cfg.resblock_kernel_sizes.push_back(gguf_get_val_u32(gctx, id));
    }
    for (uint32_t i = 0;; ++i) {
        char key[96];
        snprintf(key, sizeof key, "nsf-hifigan.resblock_dilations.%u", i);
        int64_t id = gguf_find_key(gctx, key);
        if (id < 0) break;
        cfg.resblock_dilations.push_back(gguf_get_val_u32(gctx, id));
    }

    const int64_t n_tensors = gguf_get_n_tensors(gctx);
    ggml_init_params ip {
        ggml_tensor_overhead() * (size_t)(n_tensors + 16),
        nullptr,
        true,
    };
    ctx_w = ggml_init(ip);

    auto lr = dsrt::load_gguf_weights(path, gctx, ctx_meta, ctx_w, backend, tensors);
    weights_buffer = lr.buffer;
    if (!weights_buffer) {
        gguf_free(gctx);
        ggml_free(ctx_meta);
        return false;
    }

    fprintf(stderr,
            "[load] %s tensors=%zu backend=%zu MB mel=%u init_ch=%u ups=%u resblocks=%u sr=%u hop=%u mini_nsf=%d noise=%.4g\n",
            path.c_str(), tensors.size(),
            ggml_backend_buffer_get_size(weights_buffer) / (1024 * 1024),
            cfg.num_mels, cfg.upsample_initial_channel, cfg.num_upsamples,
            cfg.num_resblocks, cfg.sampling_rate, cfg.hop_size, (int)cfg.mini_nsf,
            cfg.noise_sigma);

    gguf_free(gctx);
    ggml_free(ctx_meta);
    return true;
}

} // namespace nsv
