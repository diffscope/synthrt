#include "variance.h"
#include "runtime_backend.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace dsv {

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

static float get_f32(gguf_context * gc, const char * key, float def = 0.0f) {
    int64_t id = gguf_find_key(gc, key);
    return id < 0 ? def : gguf_get_val_f32(gc, id);
}

static bool get_bool(gguf_context * gc, const char * key, bool def = false) {
    int64_t id = gguf_find_key(gc, key);
    return id < 0 ? def : gguf_get_val_bool(gc, id);
}

static bool load_variance_targets(gguf_context * gc, Config & cfg) {
    const uint32_t n = get_u32(gc, "diffsinger-variance.variance.target_count", 0);
    cfg.variance_targets.clear();
    if (n == 0) {
        fprintf(stderr, "[fatal] variance GGUF is missing per-target metadata\n");
        return false;
    }
    for (uint32_t i = 0; i < n; ++i) {
        char key[192];
        VarianceTarget t;
        snprintf(key, sizeof key, "diffsinger-variance.variance.target.%u.name", i);
        t.name = get_str(gc, key);
        if (t.name.empty()) {
            fprintf(stderr, "[fatal] variance target %u has no name\n", i);
            return false;
        }
        snprintf(key, sizeof key, "diffsinger-variance.variance.target.%u.norm_min", i);
        t.norm_min = get_f32(gc, key, 0.0f);
        snprintf(key, sizeof key, "diffsinger-variance.variance.target.%u.norm_max", i);
        t.norm_max = get_f32(gc, key, 0.0f);
        snprintf(key, sizeof key, "diffsinger-variance.variance.target.%u.clip_min", i);
        t.clip_min = get_f32(gc, key, t.norm_min);
        snprintf(key, sizeof key, "diffsinger-variance.variance.target.%u.clip_max", i);
        t.clip_max = get_f32(gc, key, t.norm_max);
        cfg.variance_targets.push_back(t);
    }
    return true;
}

static void load_flow(gguf_context * gc, const char * prefix, FlowConfig & f) {
    char key[160];
    auto ku = [&](const char * suffix, uint32_t def = 0) {
        snprintf(key, sizeof key, "diffsinger-variance.%s.%s", prefix, suffix);
        return get_u32(gc, key, def);
    };
    auto ks = [&](const char * suffix, const char * def = "") {
        snprintf(key, sizeof key, "diffsinger-variance.%s.%s", prefix, suffix);
        return get_str(gc, key, def);
    };
    auto kb = [&](const char * suffix, bool def = false) {
        snprintf(key, sizeof key, "diffsinger-variance.%s.%s", prefix, suffix);
        return get_bool(gc, key, def);
    };

    f.backbone_type = ks("backbone_type");
    f.repeat_bins = ku("repeat_bins", 0);
    f.total_repeat_bins = ku("total_repeat_bins", 0);
    f.channels = ku("backbone.num_channels", 0);
    f.layers = ku("backbone.num_layers", 0);
    f.kernel_size = ku("backbone.kernel_size", 0);
    f.expansion_factor = ku("backbone.expansion_factor", 1);
    f.glu_type = ks("backbone.glu_type");
    f.use_conditioner_cache = kb("backbone.use_conditioner_cache", false);
}

bool Model::load(const std::string & path) {
    backend = dsrt::init_backend("variance");
    if (!backend) return false;

    ggml_context * ctx_meta = nullptr;
    gguf_init_params gp { /*no_alloc=*/true, /*ctx=*/ &ctx_meta };
    gguf_context * gctx = gguf_init_from_file(path.c_str(), gp);
    if (!gctx) {
        fprintf(stderr, "[fatal] gguf_init_from_file failed: %s\n", path.c_str());
        return false;
    }

    std::string arch = get_str(gctx, "general.architecture");
    if (arch != "diffsinger-variance") {
        fprintf(stderr, "[fatal] expected diffsinger-variance GGUF, got '%s'\n", arch.c_str());
        gguf_free(gctx);
        ggml_free(ctx_meta);
        return false;
    }

    cfg.hidden_size = get_u32(gctx, "diffsinger-variance.hidden_size", 0);
    cfg.enc_layers = get_u32(gctx, "diffsinger-variance.enc_layers", 0);
    cfg.num_heads = get_u32(gctx, "diffsinger-variance.num_heads", 0);
    cfg.enc_ffn_kernel_size = get_u32(gctx, "diffsinger-variance.enc_ffn_kernel_size", 0);
    cfg.ffn_act = get_str(gctx, "diffsinger-variance.ffn_act");
    cfg.use_rope = get_bool(gctx, "diffsinger-variance.use_rope", false);
    cfg.rope_interleaved = get_bool(gctx, "diffsinger-variance.rope_interleaved", true);
    cfg.use_lang_id = get_bool(gctx, "diffsinger-variance.use_lang_id", false);
    cfg.use_spk_id = get_bool(gctx, "diffsinger-variance.use_spk_id", false);
    cfg.use_variance_scaling = get_bool(gctx, "diffsinger-variance.use_variance_scaling", false);
    cfg.diffusion_type = get_str(gctx, "diffsinger-variance.diffusion_type");
    cfg.time_scale_factor = get_u32(gctx, "diffsinger-variance.time_scale_factor", 1000);
    cfg.sampling_algorithm = get_str(gctx, "diffsinger-variance.sampling_algorithm", "euler");
    cfg.sampling_steps = get_u32(gctx, "diffsinger-variance.sampling_steps", 20);
    cfg.vocab_size = get_u32(gctx, "diffsinger-variance.vocab_size", 0);
    cfg.num_spk = get_u32(gctx, "diffsinger-variance.num_spk", 0);
    cfg.num_lang = get_u32(gctx, "diffsinger-variance.num_lang", 0);
    cfg.predict_dur = get_bool(gctx, "diffsinger-variance.predict_dur", false);
    cfg.predict_energy = get_bool(gctx, "diffsinger-variance.predict_energy", false);
    cfg.predict_breathiness = get_bool(gctx, "diffsinger-variance.predict_breathiness", false);
    cfg.predict_voicing = get_bool(gctx, "diffsinger-variance.predict_voicing", false);
    cfg.predict_tension = get_bool(gctx, "diffsinger-variance.predict_tension", false);
    load_flow(gctx, "variance", cfg.variance);
    if (!load_variance_targets(gctx, cfg)) {
        gguf_free(gctx);
        ggml_free(ctx_meta);
        return false;
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
            "[load] %s tensors=%zu backend=%zu MB H=%u vocab=%u spk=%u\n",
            path.c_str(), tensors.size(),
            ggml_backend_buffer_get_size(weights_buffer) / (1024 * 1024),
            cfg.hidden_size, cfg.vocab_size, cfg.num_spk);
    fprintf(stderr,
            "[load] variance %s C=%u L=%u K=%u total_R=%u glu=%s cond_cache=%d\n",
            cfg.variance.backbone_type.c_str(), cfg.variance.channels, cfg.variance.layers,
            cfg.variance.kernel_size, cfg.variance.total_repeat_bins,
            cfg.variance.glu_type.c_str(), (int)cfg.variance.use_conditioner_cache);
    if (!cfg.variance_targets.empty()) {
        fprintf(stderr, "[load] variance targets:");
        for (const auto & t : cfg.variance_targets) {
            fprintf(stderr, " %s", t.name.c_str());
        }
        fprintf(stderr, "\n");
    }

    gguf_free(gctx);
    ggml_free(ctx_meta);
    return true;
}

} // namespace dsv
