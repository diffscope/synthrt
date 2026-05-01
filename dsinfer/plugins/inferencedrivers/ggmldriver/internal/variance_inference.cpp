#include "variance.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace dsv {

static std::string tn(const char * fmt, int a) {
    char buf[128];
    snprintf(buf, sizeof buf, fmt, a);
    return buf;
}

static ggml_tensor * linear(ggml_context * ctx,
                            const Model & m,
                            const std::string & prefix,
                            ggml_tensor * x) {
    ggml_tensor * y = ggml_mul_mat(ctx, m.get(prefix + ".weight"), x);
    return ggml_add(ctx, y, m.get(prefix + ".bias"));
}

static ggml_tensor * conv1d_k1_as_linear(ggml_context * ctx,
                                         const Model & m,
                                         const std::string & prefix,
                                         ggml_tensor * x) {
    ggml_tensor * w = m.get(prefix + ".weight");
    ggml_tensor * w2 = ggml_reshape_2d(ctx, w, w->ne[1], w->ne[2]);
    ggml_tensor * y = ggml_mul_mat(ctx, w2, x);
    return ggml_add(ctx, y, m.get(prefix + ".bias"));
}


static ggml_tensor * atanglu(ggml_context * ctx, ggml_tensor * x) {
    const int64_t c = x->ne[0] / 2;
    ggml_tensor * out = ggml_view_2d(ctx, x, c, x->ne[1], x->nb[1], 0);
    ggml_tensor * gate = ggml_view_2d(ctx, x, c, x->ne[1], x->nb[1], c * x->nb[0]);
    // Branchless atan(gate) using stock ggml ops so the graph runs on Metal/CUDA.
    // See ggml_acoustic/src/inference.cpp::atanglu for derivation.
    const float pi_half = 1.5707963267948966f;
    ggml_tensor * sx  = ggml_sgn(ctx, gate);
    ggml_tensor * ax  = ggml_abs(ctx, gate);
    ggml_tensor * eps = ggml_scale_bias(ctx, ax, 1.0f, 1e-12f);
    ggml_tensor * ones = ggml_scale_bias(ctx, ax, 0.0f, 1.0f);
    ggml_tensor * recip = ggml_div(ctx, ones, eps);
    ggml_tensor * ax_m1 = ggml_scale_bias(ctx, ax, 1.0f, -1.0f);
    ggml_tensor * big   = ggml_step(ctx, ax_m1);
    ggml_tensor * sml   = ggml_scale_bias(ctx, big, -1.0f, 1.0f);
    ggml_tensor * y = ggml_add(ctx,
        ggml_mul(ctx, sml, ax),
        ggml_mul(ctx, big, recip));
    ggml_tensor * y2 = ggml_mul(ctx, y, y);
    auto fma = [&](ggml_tensor * a, ggml_tensor * b, float c_) {
        return ggml_scale_bias(ctx, ggml_mul(ctx, a, b), 1.0f, c_);
    };
    ggml_tensor * p = ggml_scale_bias(ctx, y2, 0.0f,  0.020835f);
    p = fma(p, y2, -0.085133f);
    p = fma(p, y2,  0.180141f);
    p = fma(p, y2, -0.330299f);
    p = fma(p, y2,  0.999866f);
    ggml_tensor * poly = ggml_mul(ctx, p, y);
    ggml_tensor * neg_poly = ggml_scale_bias(ctx, poly, -1.0f, pi_half);
    ggml_tensor * atan_pos = ggml_add(ctx,
        ggml_mul(ctx, sml, poly),
        ggml_mul(ctx, big, neg_poly));
    ggml_tensor * atan_g = ggml_mul(ctx, sx, atan_pos);
    return ggml_mul(ctx, out, atan_g);
}

static ggml_tensor * encoder_layer(ggml_context * ctx,
                                   const Model & m,
                                   const std::string & pfx,
                                   ggml_tensor * x,
                                   ggml_tensor * nonpad,
                                   ggml_tensor * attn_mask,
                                   ggml_tensor * pos,
                                   int layer,
                                   int hidden,
                                   int heads,
                                   int ffn_kernel,
                                   const std::string & ffn_act,
                                   bool use_rope,
                                   bool rope_interleaved) {
    const int len = (int)x->ne[1];
    const int head_dim = hidden / heads;
    ggml_tensor * res = x;

    ggml_tensor * h = ggml_norm(ctx, x, 1e-5f);
    h = ggml_add(ctx,
        ggml_mul(ctx, h, m.get(pfx + tn(".enc.%d.ln1.weight", layer))),
        m.get(pfx + tn(".enc.%d.ln1.bias", layer)));

    ggml_tensor * qkv = ggml_mul_mat(ctx, m.get(pfx + tn(".enc.%d.attn.in_proj.weight", layer)), h);
    ggml_tensor * q = ggml_cont(ctx, ggml_view_2d(ctx, qkv, hidden, len, qkv->nb[1], 0));
    ggml_tensor * k = ggml_cont(ctx, ggml_view_2d(ctx, qkv, hidden, len, qkv->nb[1], hidden * sizeof(float)));
    ggml_tensor * v = ggml_cont(ctx, ggml_view_2d(ctx, qkv, hidden, len, qkv->nb[1], 2 * hidden * sizeof(float)));

    ggml_tensor * q3 = ggml_reshape_3d(ctx, q, head_dim, heads, len);
    ggml_tensor * k3 = ggml_reshape_3d(ctx, k, head_dim, heads, len);
    ggml_tensor * v3 = ggml_reshape_3d(ctx, v, head_dim, heads, len);
    if (use_rope) {
        const int rope_mode = rope_interleaved ? 0 : 2;
        q3 = ggml_rope_ext(ctx, ggml_cont(ctx, q3), pos, nullptr,
                           head_dim, rope_mode, 0, 10000.0f, 1.0f, 0.0f, 1.0f, 32.0f, 1.0f);
        k3 = ggml_rope_ext(ctx, ggml_cont(ctx, k3), pos, nullptr,
                           head_dim, rope_mode, 0, 10000.0f, 1.0f, 0.0f, 1.0f, 32.0f, 1.0f);
    }
    q = ggml_cont(ctx, ggml_permute(ctx, q3, 0, 2, 1, 3));
    k = ggml_cont(ctx, ggml_permute(ctx, k3, 0, 2, 1, 3));
    v = ggml_cont(ctx, ggml_permute(ctx, v3, 1, 2, 0, 3));

    ggml_tensor * sc = ggml_scale(ctx, ggml_mul_mat(ctx, k, q), 1.0f / std::sqrt((float)head_dim));
    sc = ggml_add(ctx, sc, ggml_reshape_3d(ctx, attn_mask, len, 1, 1));
    sc = ggml_soft_max(ctx, sc);
    ggml_tensor * ao = ggml_mul_mat(ctx, v, sc);
    ao = ggml_cont(ctx, ggml_permute(ctx, ao, 0, 2, 1, 3));
    ao = ggml_reshape_2d(ctx, ao, hidden, len);
    h = ggml_mul_mat(ctx, m.get(pfx + tn(".enc.%d.attn.out_proj.weight", layer)), ao);
    x = ggml_mul(ctx, ggml_add(ctx, res, h), nonpad);

    res = x;
    h = ggml_norm(ctx, x, 1e-5f);
    h = ggml_add(ctx,
        ggml_mul(ctx, h, m.get(pfx + tn(".enc.%d.ln2.weight", layer))),
        m.get(pfx + tn(".enc.%d.ln2.bias", layer)));
    ggml_tensor * ht = ggml_cont(ctx, ggml_transpose(ctx, h));
    ggml_tensor * ff = ggml_conv_1d(ctx,
        ggml_cast(ctx, m.get(pfx + tn(".enc.%d.ffn1.weight", layer)), GGML_TYPE_F16),
        ht, 1, ffn_kernel / 2, 1);
    ff = ggml_cont(ctx, ggml_transpose(ctx, ff));
    ff = ggml_add(ctx, ff, m.get(pfx + tn(".enc.%d.ffn1.bias", layer)));
    ff = ggml_scale(ctx, ff, 1.0f / std::sqrt((float)ffn_kernel));
    if (ffn_act == "gelu") {
        ff = ggml_gelu_erf(ctx, ff);
    } else if (ffn_act == "relu") {
        ff = ggml_relu(ctx, ff);
    } else if (ffn_act == "swish") {
        ff = ggml_silu(ctx, ff);
    } else {
        ff = ggml_gelu_erf(ctx, ff);
    }
    ff = ggml_add(ctx,
        ggml_mul_mat(ctx, m.get(pfx + tn(".enc.%d.ffn2.weight", layer)), ff),
        m.get(pfx + tn(".enc.%d.ffn2.bias", layer)));
    return ggml_mul(ctx, ggml_add(ctx, res, ff), nonpad);
}

static ggml_tensor * run_encoder_graph(ggml_context * ctx,
                                       const Model & m,
                                       const std::string & pfx,
                                       ggml_tensor * main_embed,
                                       ggml_tensor * extra_embed,
                                       ggml_tensor * nonpad,
                                       ggml_tensor * attn_mask,
                                       ggml_tensor * pos,
                                       int layers,
                                       int hidden,
                                       int heads,
                                       int ffn_kernel,
                                       const std::string & ffn_act,
                                       bool use_rope,
                                       bool rope_interleaved) {
    ggml_tensor * x = ggml_scale(ctx, main_embed, std::sqrt((float)hidden));
    x = ggml_add(ctx, x, extra_embed);
    x = ggml_mul(ctx, x, nonpad);
    for (int li = 0; li < layers; ++li) {
        x = encoder_layer(ctx, m, pfx, x, nonpad, attn_mask, pos, li, hidden, heads,
                          ffn_kernel, ffn_act, use_rope, rope_interleaved);
    }
    x = ggml_norm(ctx, x, 1e-5f);
    x = ggml_add(ctx,
        ggml_mul(ctx, x, m.get(pfx + ".enc.final_ln.weight")),
        m.get(pfx + ".enc.final_ln.bias"));
    return ggml_mul(ctx, x, nonpad);
}

bool run_fs2_condition(const Model & m, const FS2ConditionInputs & in, ConditionOutputs & out) {
    const Config & c = m.cfg;
    const int L = in.phones;
    const int T = in.frames;
    const int H = (int)c.hidden_size;
    if (L <= 0 || T <= 0) {
        fprintf(stderr, "[err] phones and frames must be positive\n");
        return false;
    }
    if (in.tokens.size() != (size_t)L) {
        fprintf(stderr, "[err] token size mismatch\n");
        return false;
    }
    if (in.ph2word.size() != (size_t)L || in.word_dur.empty()) {
        fprintf(stderr, "[err] fs2 condition requires ph2word and word_dur\n");
        return false;
    }
    if (in.mel2ph.size() != (size_t)T) {
        fprintf(stderr, "[err] mel2ph size mismatch\n");
        return false;
    }
    if (!in.languages.empty() && in.languages.size() != (size_t)L) {
        fprintf(stderr, "[err] language size mismatch\n");
        return false;
    }
    if (c.use_lang_id && in.languages.empty()) {
        fprintf(stderr, "[err] model requires languages, but --lang-bin was not provided\n");
        return false;
    }

    std::vector<float> dur((size_t)L);
    std::vector<int32_t> onset((size_t)L);
    for (int i = 0; i < L; ++i) {
        const int wi = in.ph2word[(size_t)i];
        if (wi <= 0 || wi > (int)in.word_dur.size()) {
            fprintf(stderr, "[err] ph2word index out of range at phone %d: %d\n", i, wi);
            return false;
        }
        onset[(size_t)i] = (i == 0 || wi != in.ph2word[(size_t)i - 1]) ? 1 : 0;
        dur[(size_t)i] = in.word_dur[(size_t)wi - 1];
    }
    std::vector<float> nonpad((size_t)L);
    std::vector<float> attn_mask((size_t)L);
    for (int i = 0; i < L; ++i) {
        const bool keep = in.tokens[i] != 0;
        nonpad[i] = keep ? 1.0f : 0.0f;
        attn_mask[i] = keep ? 0.0f : -INFINITY;
    }
    std::vector<float> zeros((size_t)H, 0.0f);
    std::vector<int32_t> spk_index(1, std::max(0, in.spk_id));

    ggml_init_params ip_in { ggml_tensor_overhead() * 16, nullptr, true };
    ggml_context * ctx_in = ggml_init(ip_in);
    ggml_tensor * tok_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_I32, L);
    ggml_set_input(tok_t);
    ggml_set_name(tok_t, "tok");
    ggml_tensor * dur_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, 1, L);
    ggml_set_input(dur_t);
    ggml_set_name(dur_t, "dur");
    ggml_tensor * onset_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_I32, L);
    ggml_set_input(onset_t);
    ggml_set_name(onset_t, "onset");
    ggml_tensor * mel2ph_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_I32, T);
    ggml_set_input(mel2ph_t);
    ggml_set_name(mel2ph_t, "mel2ph");
    ggml_tensor * nonpad_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, 1, L);
    ggml_set_input(nonpad_t);
    ggml_set_name(nonpad_t, "nonpad");
    ggml_tensor * attn_mask_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_F32, L);
    ggml_set_input(attn_mask_t);
    ggml_set_name(attn_mask_t, "attn_mask");
    ggml_tensor * pos_t = nullptr;
    if (c.use_rope) {
        pos_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_I32, L);
        ggml_set_input(pos_t);
        ggml_set_name(pos_t, "pos");
    }
    ggml_tensor * zero_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, H, 1);
    ggml_set_input(zero_t);
    ggml_set_name(zero_t, "zero");
    ggml_tensor * lang_t = nullptr;
    if (c.use_lang_id) {
        lang_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_I32, L);
        ggml_set_input(lang_t);
        ggml_set_name(lang_t, "lang");
    }
    ggml_tensor * spk_t = nullptr;
    if (c.use_spk_id && in.spk_id >= 0) {
        spk_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_I32, 1);
        ggml_set_input(spk_t);
        ggml_set_name(spk_t, "spk");
    }

    ggml_init_params ip { 768ULL * 1024 * 1024, nullptr, true };
    ggml_context * ctx = ggml_init(ip);
    ggml_cgraph * gf = ggml_new_graph_custom(ctx, GGML_DEFAULT_GRAPH_SIZE * 64, false);

    ggml_tensor * main = ggml_get_rows(ctx, m.get("fs2.txt_embed.weight"), tok_t);
    ggml_tensor * extra = ggml_get_rows(ctx, m.get("fs2.onset_embed.weight"), onset_t);
    ggml_tensor * word_dur_emb = ggml_add(ctx,
        ggml_mul_mat(ctx, m.get("fs2.word_dur_embed.weight"), dur_t),
        m.get("fs2.word_dur_embed.bias"));
    extra = ggml_add(ctx, extra, word_dur_emb);
    if (c.use_lang_id) {
        extra = ggml_add(ctx, extra, ggml_get_rows(ctx, m.get("fs2.lang_embed.weight"), lang_t));
    }

    ggml_tensor * enc = run_encoder_graph(ctx, m, "fs2", main, extra, nonpad_t, attn_mask_t, pos_t,
                                          (int)c.enc_layers, H, (int)c.num_heads,
                                          (int)c.enc_ffn_kernel_size, c.ffn_act,
                                          c.use_rope, c.rope_interleaved);
    ggml_tensor * cond = ggml_get_rows(ctx, ggml_concat(ctx, zero_t, enc, 1), mel2ph_t);
    if (c.use_spk_id && in.spk_id >= 0) {
        cond = ggml_add(ctx, cond, ggml_get_rows(ctx, m.get("spk_embed.weight"), spk_t));
    }
    ggml_set_output(cond);
    ggml_set_name(cond, "fs2_condition");
    ggml_build_forward_expand(gf, cond);

    ggml_backend_buffer_t in_buf = ggml_backend_alloc_ctx_tensors(ctx_in, m.backend);
    if (!in_buf) {
        fprintf(stderr, "[fatal] fs2 input buffer alloc failed\n");
        ggml_free(ctx);
        ggml_free(ctx_in);
        return false;
    }
    ggml_gallocr_t ga = ggml_gallocr_new(ggml_backend_get_default_buffer_type(m.backend));
    if (!ggml_gallocr_alloc_graph(ga, gf)) {
        fprintf(stderr, "[fatal] fs2 graph alloc failed\n");
        ggml_gallocr_free(ga);
        ggml_backend_buffer_free(in_buf);
        ggml_free(ctx);
        ggml_free(ctx_in);
        return false;
    }

    ggml_backend_tensor_set(tok_t, in.tokens.data(), 0, in.tokens.size() * sizeof(int32_t));
    ggml_backend_tensor_set(dur_t, dur.data(), 0, dur.size() * sizeof(float));
    ggml_backend_tensor_set(onset_t, onset.data(), 0, onset.size() * sizeof(int32_t));
    ggml_backend_tensor_set(mel2ph_t, in.mel2ph.data(), 0, in.mel2ph.size() * sizeof(int32_t));
    ggml_backend_tensor_set(nonpad_t, nonpad.data(), 0, nonpad.size() * sizeof(float));
    ggml_backend_tensor_set(attn_mask_t, attn_mask.data(), 0, attn_mask.size() * sizeof(float));
    if (pos_t && pos_t->buffer) {
        std::vector<int32_t> positions((size_t)L);
        for (int i = 0; i < L; ++i) positions[i] = i;
        ggml_backend_tensor_set(pos_t, positions.data(), 0, positions.size() * sizeof(int32_t));
    }
    ggml_backend_tensor_set(zero_t, zeros.data(), 0, zeros.size() * sizeof(float));
    if (lang_t) {
        ggml_backend_tensor_set(lang_t, in.languages.data(), 0, in.languages.size() * sizeof(int32_t));
    }
    if (spk_t) {
        ggml_backend_tensor_set(spk_t, spk_index.data(), 0, sizeof(int32_t));
    }

    ggml_status st = ggml_backend_graph_compute(m.backend, gf);
    if (st != GGML_STATUS_SUCCESS) {
        fprintf(stderr, "[fatal] fs2 condition compute = %d\n", (int)st);
        ggml_gallocr_free(ga);
        ggml_backend_buffer_free(in_buf);
        ggml_free(ctx);
        ggml_free(ctx_in);
        return false;
    }

    out.frames = T;
    out.hidden = H;
    out.condition.resize((size_t)T * H);
    ggml_backend_tensor_get(cond, out.condition.data(), 0, out.condition.size() * sizeof(float));

    ggml_gallocr_free(ga);
    ggml_backend_buffer_free(in_buf);
    ggml_free(ctx);
    ggml_free(ctx_in);
    return true;
}

static ggml_tensor * lynx_block(ggml_context * ctx,
                                const Model & m,
                                const std::string & pfx,
                                ggml_tensor * x,
                                int layer,
                                int kernel_size) {
    ggml_tensor * h = ggml_norm(ctx, x, 1e-5f);
    h = ggml_add(ctx,
        ggml_mul(ctx, h, m.get(pfx + tn(".vf.r.%d.net.0.weight", layer))),
        m.get(pfx + tn(".vf.r.%d.net.0.bias", layer)));

    ggml_tensor * w = ggml_cast(ctx, m.get(pfx + tn(".vf.r.%d.net.2.weight", layer)), GGML_TYPE_F16);
    ggml_tensor * ht = ggml_cont(ctx, ggml_transpose(ctx, h)); // [T, C]
    h = ggml_conv_1d_dw(ctx, w, ht, 1, kernel_size / 2, 1);
    h = ggml_cont(ctx, ggml_transpose(ctx, h)); // [C, T]
    h = ggml_add(ctx, h, m.get(pfx + tn(".vf.r.%d.net.2.bias", layer)));

    h = atanglu(ctx, linear(ctx, m, pfx + tn(".vf.r.%d.net.4", layer), h));
    h = atanglu(ctx, linear(ctx, m, pfx + tn(".vf.r.%d.net.6", layer), h));
    h = linear(ctx, m, pfx + tn(".vf.r.%d.net.8", layer), h);
    return ggml_add(ctx, x, h);
}

struct VelocityInputs {
    int frames = 0;
    int bins = 0;
    float diffusion_step = 0.0f;
    std::vector<float> spec; // [T, bins] row-major
    std::vector<float> cond; // [T, hidden_size] row-major
};

struct VelocityOutputs {
    int frames = 0;
    int bins = 0;
    std::vector<float> velocity; // [T, bins] row-major
};

static std::vector<float> sinusoidal_embedding(int dim, float x) {
    std::vector<float> out((size_t)dim);
    const int half = dim / 2;
    const float denom = half > 1 ? (float)(half - 1) : 1.0f;
    const float scale = std::log(10000.0f) / denom;
    for (int i = 0; i < half; ++i) {
        const float v = x * std::exp(-scale * i);
        out[(size_t)i] = std::sin(v);
        out[(size_t)half + i] = std::cos(v);
    }
    return out;
}

static bool run_velocity(const Model & m, const VelocityInputs & in, VelocityOutputs & out) {
    const Config & c = m.cfg;
    const FlowConfig * fc = &c.variance;
    if (fc->backbone_type != "lynxnet2") {
        fprintf(stderr, "[err] only lynxnet2 velocity backbone is implemented\n");
        return false;
    }
    if (fc->glu_type != "atanglu") {
        fprintf(stderr, "[err] only atanglu LYNXNet2 blocks are implemented for this checkpoint\n");
        return false;
    }
    if (in.frames <= 0 || in.bins <= 0) {
        fprintf(stderr, "[err] frames and bins must be positive\n");
        return false;
    }
    if ((uint32_t)in.bins != fc->total_repeat_bins) {
        fprintf(stderr, "[err] variance bins mismatch: got %d expected %u\n",
                in.bins, fc->total_repeat_bins);
        return false;
    }
    if (in.spec.size() != (size_t)in.frames * in.bins) {
        fprintf(stderr, "[err] spec size mismatch\n");
        return false;
    }
    if (in.cond.size() != (size_t)in.frames * c.hidden_size) {
        fprintf(stderr, "[err] cond size mismatch\n");
        return false;
    }

    std::vector<float> diff = sinusoidal_embedding((int)fc->channels, in.diffusion_step);

    ggml_init_params ip_in { ggml_tensor_overhead() * 8, nullptr, true };
    ggml_context * ctx_in = ggml_init(ip_in);
    ggml_tensor * spec_in = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, in.bins, in.frames);
    ggml_set_input(spec_in);
    ggml_set_name(spec_in, "spec");
    ggml_tensor * cond_in = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, c.hidden_size, in.frames);
    ggml_set_input(cond_in);
    ggml_set_name(cond_in, "cond");
    ggml_tensor * diff_in = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, fc->channels, 1);
    ggml_set_input(diff_in);
    ggml_set_name(diff_in, "diff");

    ggml_init_params ip { 768ULL * 1024 * 1024, nullptr, true };
    ggml_context * ctx = ggml_init(ip);
    ggml_cgraph * gf = ggml_new_graph_custom(ctx, GGML_DEFAULT_GRAPH_SIZE * 64, false);

    const std::string pfx = "var";
    ggml_tensor * x = linear(ctx, m, pfx + ".vf.in", spec_in);
    if (fc->use_conditioner_cache) {
        x = ggml_add(ctx, x, conv1d_k1_as_linear(ctx, m, pfx + ".vf.cond", cond_in));
    } else {
        x = ggml_add(ctx, x, linear(ctx, m, pfx + ".vf.cond", cond_in));
    }

    ggml_tensor * d = linear(ctx, m, pfx + ".vf.diff.1", diff_in);
    d = ggml_gelu_erf(ctx, d);
    d = linear(ctx, m, pfx + ".vf.diff.3", d);
    x = ggml_add(ctx, x, d);

    for (uint32_t li = 0; li < fc->layers; ++li) {
        x = lynx_block(ctx, m, pfx, x, (int)li, (int)fc->kernel_size);
    }
    x = ggml_norm(ctx, x, 1e-5f);
    x = ggml_add(ctx,
        ggml_mul(ctx, x, m.get(pfx + ".vf.norm.weight")),
        m.get(pfx + ".vf.norm.bias"));
    x = linear(ctx, m, pfx + ".vf.out", x);
    ggml_set_output(x);
    ggml_set_name(x, "velocity");
    ggml_build_forward_expand(gf, x);

    ggml_backend_buffer_t in_buf = ggml_backend_alloc_ctx_tensors(ctx_in, m.backend);
    if (!in_buf) {
        fprintf(stderr, "[fatal] velocity input buffer alloc failed\n");
        ggml_free(ctx);
        ggml_free(ctx_in);
        return false;
    }
    ggml_gallocr_t ga = ggml_gallocr_new(ggml_backend_get_default_buffer_type(m.backend));
    if (!ggml_gallocr_alloc_graph(ga, gf)) {
        fprintf(stderr, "[fatal] velocity graph alloc failed\n");
        ggml_gallocr_free(ga);
        ggml_backend_buffer_free(in_buf);
        ggml_free(ctx);
        ggml_free(ctx_in);
        return false;
    }

    ggml_backend_tensor_set(spec_in, in.spec.data(), 0, in.spec.size() * sizeof(float));
    ggml_backend_tensor_set(cond_in, in.cond.data(), 0, in.cond.size() * sizeof(float));
    ggml_backend_tensor_set(diff_in, diff.data(), 0, diff.size() * sizeof(float));
    ggml_status st = ggml_backend_graph_compute(m.backend, gf);
    if (st != GGML_STATUS_SUCCESS) {
        fprintf(stderr, "[fatal] velocity compute = %d\n", (int)st);
        ggml_gallocr_free(ga);
        ggml_backend_buffer_free(in_buf);
        ggml_free(ctx);
        ggml_free(ctx_in);
        return false;
    }

    out.frames = in.frames;
    out.bins = in.bins;
    out.velocity.resize((size_t)in.frames * in.bins);
    ggml_backend_tensor_get(x, out.velocity.data(), 0, out.velocity.size() * sizeof(float));

    ggml_gallocr_free(ga);
    ggml_backend_buffer_free(in_buf);
    ggml_free(ctx);
    ggml_free(ctx_in);
    return true;
}

bool compose_variance_condition(const Model & m, const VarianceConditionInputs & in, ConditionOutputs & out) {
    const Config & c = m.cfg;
    const int T = in.frames;
    const int H = (int)c.hidden_size;
    if (T <= 0) {
        fprintf(stderr, "[err] frames must be positive\n");
        return false;
    }
    if (in.fs2_condition.size() != (size_t)T * H || in.pitch.size() != (size_t)T) {
        fprintf(stderr, "[err] variance condition input size mismatch\n");
        return false;
    }
    std::vector<float> pitch = in.pitch;
    if (c.use_variance_scaling) {
        for (float & v : pitch) v /= 12.0f;
    }

    ggml_init_params ip_in { ggml_tensor_overhead() * 4, nullptr, true };
    ggml_context * ctx_in = ggml_init(ip_in);
    ggml_tensor * fs2_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, H, T);
    ggml_set_input(fs2_t);
    ggml_set_name(fs2_t, "fs2_condition");
    ggml_tensor * pitch_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, 1, T);
    ggml_set_input(pitch_t);
    ggml_set_name(pitch_t, "pitch");

    ggml_init_params ip { 64ULL * 1024 * 1024, nullptr, true };
    ggml_context * ctx = ggml_init(ip);
    ggml_cgraph * gf = ggml_new_graph_custom(ctx, GGML_DEFAULT_GRAPH_SIZE * 4, false);

    ggml_tensor * emb = ggml_add(ctx,
        ggml_mul_mat(ctx, m.get("pitch_embed.weight"), pitch_t),
        m.get("pitch_embed.bias"));
    ggml_tensor * cond = ggml_add(ctx, fs2_t, emb);
    ggml_set_output(cond);
    ggml_set_name(cond, "variance_condition");
    ggml_build_forward_expand(gf, cond);

    ggml_backend_buffer_t in_buf = ggml_backend_alloc_ctx_tensors(ctx_in, m.backend);
    if (!in_buf) {
        fprintf(stderr, "[fatal] variance condition input buffer alloc failed\n");
        ggml_free(ctx);
        ggml_free(ctx_in);
        return false;
    }
    ggml_gallocr_t ga = ggml_gallocr_new(ggml_backend_get_default_buffer_type(m.backend));
    if (!ggml_gallocr_alloc_graph(ga, gf)) {
        fprintf(stderr, "[fatal] variance condition graph alloc failed\n");
        ggml_gallocr_free(ga);
        ggml_backend_buffer_free(in_buf);
        ggml_free(ctx);
        ggml_free(ctx_in);
        return false;
    }

    ggml_backend_tensor_set(fs2_t, in.fs2_condition.data(), 0, in.fs2_condition.size() * sizeof(float));
    ggml_backend_tensor_set(pitch_t, pitch.data(), 0, pitch.size() * sizeof(float));
    ggml_status st = ggml_backend_graph_compute(m.backend, gf);
    if (st != GGML_STATUS_SUCCESS) {
        fprintf(stderr, "[fatal] variance condition compute = %d\n", (int)st);
        ggml_gallocr_free(ga);
        ggml_backend_buffer_free(in_buf);
        ggml_free(ctx);
        ggml_free(ctx_in);
        return false;
    }

    out.frames = T;
    out.hidden = H;
    out.condition.resize((size_t)T * H);
    ggml_backend_tensor_get(cond, out.condition.data(), 0, out.condition.size() * sizeof(float));

    ggml_gallocr_free(ga);
    ggml_backend_buffer_free(in_buf);
    ggml_free(ctx);
    ggml_free(ctx_in);
    return true;
}

bool sample_variances(const Model & m, const VarianceSampleInputs & in, VarianceSampleOutputs & out) {
    const Config & c = m.cfg;
    const FlowConfig & fc = c.variance;
    const int T = in.frames;
    const int total_R = (int)fc.total_repeat_bins;
    const int per_R = (int)fc.repeat_bins;
    const int F = (int)c.variance_targets.size();
    if (T <= 0 || total_R <= 0 || per_R <= 0 || F <= 0) {
        fprintf(stderr, "[err] frames, variance targets and repeat bins must be positive\n");
        return false;
    }
    if (total_R != per_R * F) {
        fprintf(stderr, "[err] variance bin layout mismatch: total=%d repeat=%d targets=%d\n",
                total_R, per_R, F);
        return false;
    }
    if (in.condition.size() != (size_t)T * c.hidden_size) {
        fprintf(stderr, "[err] variance condition size mismatch\n");
        return false;
    }
    if (c.sampling_algorithm != "euler" && c.sampling_algorithm != "midpoint" &&
        c.sampling_algorithm != "rk2" && c.sampling_algorithm != "rk4") {
        fprintf(stderr, "[err] unsupported sampling algorithm: %s\n",
                c.sampling_algorithm.c_str());
        return false;
    }

    std::mt19937 rng(in.seed);
    std::normal_distribution<float> norm(0.0f, 1.0f);
    std::vector<float> x((size_t)T * total_R);
    for (float & v : x) v = norm(rng);
    if (const char * np = std::getenv("DSDIAG_VAR_NOISE")) {
        FILE * fp = std::fopen(np, "rb");
        if (!fp) {
            fprintf(stderr, "[fatal] DSDIAG_VAR_NOISE: open %s failed\n", np);
            return false;
        }
        std::fseek(fp, 0, SEEK_END);
        long sz = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        if ((size_t)sz != x.size() * sizeof(float)) {
            fprintf(stderr, "[fatal] DSDIAG_VAR_NOISE: size %ld != expected %zu\n",
                    sz, x.size() * sizeof(float));
            std::fclose(fp);
            return false;
        }
        if (std::fread(x.data(), sizeof(float), x.size(), fp) != x.size()) {
            fprintf(stderr, "[fatal] DSDIAG_VAR_NOISE: read failed\n");
            std::fclose(fp);
            return false;
        }
        std::fclose(fp);
        fprintf(stderr, "[debug] loaded variance noise from %s (%zu floats)\n",
                np, x.size());
    }
    if (const char * dp = std::getenv("DSDIAG_VAR_RAW_OUT")) {
        // Save raw post-flow x (no denorm/clamp/mean) for tap comparison.
        // We'll register an atexit-style write below. For now stash path.
        // (handled after sampling loop)
        (void)dp;
    }

    const int steps = std::max(1u, c.sampling_steps);
    const float dt = 1.0f / (float)steps;
    const std::string & alg = c.sampling_algorithm;

    auto eval_var_velocity = [&](const std::vector<float> & x_eval, float t_eval,
                                  std::vector<float> & v_out) -> bool {
        VelocityInputs vin;
        vin.frames = T;
        vin.bins = total_R;
        vin.diffusion_step = (float)c.time_scale_factor * t_eval;
        vin.spec = x_eval;
        vin.cond = in.condition;
        VelocityOutputs vout;
        if (!run_velocity(m, vin, vout)) return false;
        v_out = std::move(vout.velocity);
        return true;
    };

    for (int i = 0; i < steps; ++i) {
        float t_i = (float)i * dt;
        if (alg == "midpoint" || alg == "rk2") {
            std::vector<float> v1;
            if (!eval_var_velocity(x, t_i, v1)) return false;
            std::vector<float> x_mid(x.size());
            for (size_t j = 0; j < x.size(); ++j)
                x_mid[j] = x[j] + 0.5f * dt * v1[j];
            std::vector<float> v2;
            if (!eval_var_velocity(x_mid, t_i + 0.5f * dt, v2)) return false;
            for (size_t j = 0; j < x.size(); ++j)
                x[j] += dt * v2[j];
        } else if (alg == "rk4") {
            std::vector<float> k1, k2, k3, k4;
            if (!eval_var_velocity(x, t_i, k1)) return false;
            std::vector<float> x_tmp(x.size());
            for (size_t j = 0; j < x.size(); ++j)
                x_tmp[j] = x[j] + 0.5f * dt * k1[j];
            if (!eval_var_velocity(x_tmp, t_i + 0.5f * dt, k2)) return false;
            for (size_t j = 0; j < x.size(); ++j)
                x_tmp[j] = x[j] + 0.5f * dt * k2[j];
            if (!eval_var_velocity(x_tmp, t_i + 0.5f * dt, k3)) return false;
            for (size_t j = 0; j < x.size(); ++j)
                x_tmp[j] = x[j] + dt * k3[j];
            if (!eval_var_velocity(x_tmp, t_i + dt, k4)) return false;
            for (size_t j = 0; j < x.size(); ++j)
                x[j] += dt / 6.0f * (k1[j] + 2.f*k2[j] + 2.f*k3[j] + k4[j]);
        } else {
            // Euler
            VelocityInputs vin;
            vin.frames = T;
            vin.bins = total_R;
            vin.diffusion_step = (float)c.time_scale_factor * t_i;
            vin.spec = x;
            vin.cond = in.condition;
            VelocityOutputs vout;
            if (!run_velocity(m, vin, vout)) return false;
            for (size_t j = 0; j < x.size(); ++j)
                x[j] += dt * vout.velocity[j];
        }
    }

    out.frames = T;
    out.targets = c.variance_targets;
    out.values.assign((size_t)F, std::vector<float>((size_t)T, 0.0f));
    if (const char * dp = std::getenv("DSDIAG_VAR_RAW_OUT")) {
        FILE * fp = std::fopen(dp, "wb");
        if (fp) {
            std::fwrite(x.data(), sizeof(float), x.size(), fp);
            std::fclose(fp);
            fprintf(stderr, "[debug] wrote raw variance x[%d,%d] to %s\n",
                    T, total_R, dp);
        }
    }
    for (int f = 0; f < F; ++f) {
        const VarianceTarget & target = c.variance_targets[(size_t)f];
        for (int t = 0; t < T; ++t) {
            float sum = 0.0f;
            for (int r = 0; r < per_R; ++r) {
                const int bin = f * per_R + r;
                float v = x[(size_t)t * total_R + bin];
                v = (v + 1.0f) * 0.5f * (target.norm_max - target.norm_min) + target.norm_min;
                v = std::max(target.clip_min, std::min(target.clip_max, v));
                sum += v;
            }
            out.values[(size_t)f][(size_t)t] = sum / (float)per_R;
        }
    }
    return true;
}

} // namespace dsv
