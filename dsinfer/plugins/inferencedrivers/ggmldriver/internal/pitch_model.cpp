#include "pitch.h"
#include "runtime_backend.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace dsp_pitch {

Model::~Model() {
    if (weights_buffer) ggml_backend_buffer_free(weights_buffer);
    if (ctx_w)          ggml_free(ctx_w);
    if (backend)        ggml_backend_free(backend);
}

ggml_tensor * Model::get(const std::string & name, bool optional) const {
    auto it = tensors.find(name);
    if (it == tensors.end()) {
        if (optional) return nullptr;
        fprintf(stderr, "[fatal] missing tensor '%s' in pitch GGUF\n", name.c_str());
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

bool Model::load(const std::string & path) {
    backend = dsrt::init_backend("pitch");
    if (!backend) return false;

    ggml_context * ctx_meta = nullptr;
    gguf_init_params gp { /*no_alloc=*/true, /*ctx=*/ &ctx_meta };
    gguf_context * gctx = gguf_init_from_file(path.c_str(), gp);
    if (!gctx) {
        fprintf(stderr, "[fatal] gguf_init_from_file failed: %s\n", path.c_str());
        return false;
    }

    std::string arch = get_str(gctx, "general.architecture");
    if (arch != "diffsinger-pitch") {
        fprintf(stderr, "[fatal] expected diffsinger-pitch GGUF, got '%s'\n", arch.c_str());
        gguf_free(gctx);
        ggml_free(ctx_meta);
        return false;
    }

    const char * A = "diffsinger-pitch";
    char key[192];
    auto kstr = [&](const char * suffix, const char * def = "") {
        snprintf(key, sizeof key, "%s.%s", A, suffix);
        return get_str(gctx, key, def);
    };
    auto ku32 = [&](const char * suffix, uint32_t def = 0) {
        snprintf(key, sizeof key, "%s.%s", A, suffix);
        return get_u32(gctx, key, def);
    };
    auto kf32 = [&](const char * suffix, float def = 0.0f) {
        snprintf(key, sizeof key, "%s.%s", A, suffix);
        return get_f32(gctx, key, def);
    };
    auto kbool = [&](const char * suffix, bool def = false) {
        snprintf(key, sizeof key, "%s.%s", A, suffix);
        return get_bool(gctx, key, def);
    };

    cfg.hidden_size = ku32("hidden_size");
    cfg.enc_layers = ku32("enc_layers");
    cfg.num_heads = ku32("num_heads");
    cfg.enc_ffn_kernel_size = ku32("enc_ffn_kernel_size");
    cfg.ffn_act = kstr("ffn_act");
    cfg.use_rope = kbool("use_rope");
    cfg.rope_interleaved = kbool("rope_interleaved", true);
    cfg.use_lang_id = kbool("use_lang_id");
    cfg.use_spk_id = kbool("use_spk_id");
    cfg.use_variance_scaling = kbool("use_variance_scaling");
    cfg.use_melody_encoder = kbool("use_melody_encoder");
    cfg.diffusion_type = kstr("diffusion_type", "reflow");
    cfg.time_scale_factor = ku32("time_scale_factor", 1000);
    cfg.sampling_algorithm = kstr("sampling_algorithm", "euler");
    cfg.sampling_steps = ku32("sampling_steps", 20);
    cfg.vocab_size = ku32("vocab_size");
    cfg.num_spk = ku32("num_spk");
    cfg.num_lang = ku32("num_lang");

    // Melody encoder
    cfg.melody_hidden_size = ku32("melody.hidden_size", 128);
    cfg.melody_enc_layers = ku32("melody.enc_layers", 6);
    cfg.melody_num_heads = ku32("melody.num_heads", 2);
    cfg.melody_ffn_kernel_size = ku32("melody.enc_ffn_kernel_size", 3);

    // Pitch flow
    cfg.pitch.backbone_type = kstr("pitch.backbone_type", "lynxnet2");
    cfg.pitch.repeat_bins = ku32("pitch.repeat_bins", 96);
    cfg.pitch.channels = ku32("pitch.backbone.num_channels", 512);
    cfg.pitch.layers = ku32("pitch.backbone.num_layers", 6);
    cfg.pitch.kernel_size = ku32("pitch.backbone.kernel_size", 31);
    cfg.pitch.glu_type = kstr("pitch.backbone.glu_type", "atanglu");
    cfg.pitch.use_conditioner_cache = kbool("pitch.backbone.use_conditioner_cache");
    cfg.pitch.pitd_norm_min = kf32("pitch.pitd_norm_min", -8.0f);
    cfg.pitch.pitd_norm_max = kf32("pitch.pitd_norm_max", 8.0f);
    cfg.pitch.pitd_clip_min = kf32("pitch.pitd_clip_min", -12.0f);
    cfg.pitch.pitd_clip_max = kf32("pitch.pitd_clip_max", 12.0f);

    // Variance (voicing)
    cfg.predict_voicing = kbool("predict_voicing");
    if (cfg.predict_voicing) {
        cfg.variance.backbone_type = kstr("variance.backbone_type", "lynxnet2");
        cfg.variance.total_repeat_bins = ku32("variance.total_repeat_bins", 72);
        cfg.variance.channels = ku32("variance.backbone.num_channels", 512);
        cfg.variance.layers = ku32("variance.backbone.num_layers", 6);
        cfg.variance.kernel_size = ku32("variance.backbone.kernel_size", 31);
        cfg.variance.glu_type = kstr("variance.backbone.glu_type", "atanglu");
        cfg.variance.use_conditioner_cache = kbool("variance.backbone.use_conditioner_cache");
        cfg.variance.voicing_norm_min = kf32("variance.voicing_norm_min", -96.0f);
        cfg.variance.voicing_norm_max = kf32("variance.voicing_norm_max", -12.0f);
        cfg.variance.voicing_clip_min = kf32("variance.voicing_clip_min", -96.0f);
        cfg.variance.voicing_clip_max = kf32("variance.voicing_clip_max", 0.0f);
    }

    // Load tensors
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
            "[load] pitch %s tensors=%zu backend=%zu MB H=%u vocab=%u spk=%u\n",
            path.c_str(), tensors.size(),
            ggml_backend_buffer_get_size(weights_buffer) / (1024 * 1024),
            cfg.hidden_size, cfg.vocab_size, cfg.num_spk);
    fprintf(stderr,
            "[load] pitch flow %s C=%u L=%u K=%u R=%u glu=%s\n",
            cfg.pitch.backbone_type.c_str(), cfg.pitch.channels, cfg.pitch.layers,
            cfg.pitch.kernel_size, cfg.pitch.repeat_bins, cfg.pitch.glu_type.c_str());
    if (cfg.use_melody_encoder) {
        fprintf(stderr, "[load] melody encoder H=%u L=%u\n",
                cfg.melody_hidden_size, cfg.melody_enc_layers);
    }
    if (cfg.predict_voicing) {
        fprintf(stderr, "[load] voicing flow %s C=%u L=%u K=%u R=%u\n",
                cfg.variance.backbone_type.c_str(), cfg.variance.channels, cfg.variance.layers,
                cfg.variance.kernel_size, cfg.variance.total_repeat_bins);
    }

    gguf_free(gctx);
    ggml_free(ctx_meta);
    return true;
}

} // namespace dsp_pitch
