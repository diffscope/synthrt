#include "pitch.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace dsp_pitch {

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

// Transformer encoder layer (shared between FS2 and Melody encoders)
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

// LYNXNet2 residual block
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
    ggml_tensor * ht = ggml_cont(ctx, ggml_transpose(ctx, h));
    h = ggml_conv_1d_dw(ctx, w, ht, 1, kernel_size / 2, 1);
    h = ggml_cont(ctx, ggml_transpose(ctx, h));
    h = ggml_add(ctx, h, m.get(pfx + tn(".vf.r.%d.net.2.bias", layer)));

    h = atanglu(ctx, linear(ctx, m, pfx + tn(".vf.r.%d.net.4", layer), h));
    h = atanglu(ctx, linear(ctx, m, pfx + tn(".vf.r.%d.net.6", layer), h));
    h = linear(ctx, m, pfx + tn(".vf.r.%d.net.8", layer), h);
    return ggml_add(ctx, x, h);
}

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

// Run velocity network for pitch or variance flow
static bool run_velocity(const Model & m,
                         const std::string & pfx,
                         const FlowConfig & fc,
                         int T, int bins,
                         float diffusion_step,
                         const std::vector<float> & spec,
                         const std::vector<float> & cond,
                         int hidden_size,
                         std::vector<float> & velocity_out) {
    if (fc.backbone_type != "lynxnet2") {
        fprintf(stderr, "[err] only lynxnet2 backbone supported\n");
        return false;
    }

    std::vector<float> diff = sinusoidal_embedding((int)fc.channels, diffusion_step);

    ggml_init_params ip_in { ggml_tensor_overhead() * 8, nullptr, true };
    ggml_context * ctx_in = ggml_init(ip_in);
    ggml_tensor * spec_in = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, bins, T);
    ggml_set_input(spec_in); ggml_set_name(spec_in, "spec");
    ggml_tensor * cond_in = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, hidden_size, T);
    ggml_set_input(cond_in); ggml_set_name(cond_in, "cond");
    ggml_tensor * diff_in = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, fc.channels, 1);
    ggml_set_input(diff_in); ggml_set_name(diff_in, "diff");

    ggml_init_params ip { 768ULL * 1024 * 1024, nullptr, true };
    ggml_context * ctx = ggml_init(ip);
    ggml_cgraph * gf = ggml_new_graph_custom(ctx, GGML_DEFAULT_GRAPH_SIZE * 64, false);

    ggml_tensor * x = linear(ctx, m, pfx + ".vf.in", spec_in);
    if (fc.use_conditioner_cache) {
        x = ggml_add(ctx, x, conv1d_k1_as_linear(ctx, m, pfx + ".vf.cond", cond_in));
    } else {
        x = ggml_add(ctx, x, linear(ctx, m, pfx + ".vf.cond", cond_in));
    }

    ggml_tensor * d = linear(ctx, m, pfx + ".vf.diff.1", diff_in);
    d = ggml_gelu_erf(ctx, d);
    d = linear(ctx, m, pfx + ".vf.diff.3", d);
    x = ggml_add(ctx, x, d);

    for (uint32_t li = 0; li < fc.layers; ++li) {
        x = lynx_block(ctx, m, pfx, x, (int)li, (int)fc.kernel_size);
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
        fprintf(stderr, "[fatal] pitch velocity input buffer alloc failed\n");
        ggml_free(ctx); ggml_free(ctx_in);
        return false;
    }
    ggml_gallocr_t ga = ggml_gallocr_new(ggml_backend_get_default_buffer_type(m.backend));
    if (!ggml_gallocr_alloc_graph(ga, gf)) {
        fprintf(stderr, "[fatal] pitch velocity graph alloc failed\n");
        ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
        ggml_free(ctx); ggml_free(ctx_in);
        return false;
    }

    ggml_backend_tensor_set(spec_in, spec.data(), 0, spec.size() * sizeof(float));
    ggml_backend_tensor_set(cond_in, cond.data(), 0, cond.size() * sizeof(float));
    ggml_backend_tensor_set(diff_in, diff.data(), 0, diff.size() * sizeof(float));
    ggml_status st = ggml_backend_graph_compute(m.backend, gf);
    if (st != GGML_STATUS_SUCCESS) {
        fprintf(stderr, "[fatal] pitch velocity compute = %d\n", (int)st);
        ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
        ggml_free(ctx); ggml_free(ctx_in);
        return false;
    }

    velocity_out.resize((size_t)T * bins);
    ggml_backend_tensor_get(x, velocity_out.data(), 0, velocity_out.size() * sizeof(float));

    ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
    ggml_free(ctx); ggml_free(ctx_in);
    return true;
}

// ---------------------------------------------------------------------------
// FS2 Encoder (pitch model uses ph_dur_embed, not word_dur + onset)
// ---------------------------------------------------------------------------
static bool run_fs2_encoder(const Model & m, const PitchInputs & in,
                            std::vector<float> & condition_out) {
    const Config & c = m.cfg;
    const int L = in.phones;
    const int T = in.frames;
    const int H = (int)c.hidden_size;

    // Derive per-phone duration (word_dur broadcast via ph2word)
    std::vector<float> ph_dur_scaled(L);
    for (int i = 0; i < L; ++i) {
        ph_dur_scaled[i] = in.ph_dur[i];
    }

    std::vector<float> nonpad(L);
    std::vector<float> attn_mask(L);
    for (int i = 0; i < L; ++i) {
        const bool keep = in.tokens[i] != 0;
        nonpad[i] = keep ? 1.0f : 0.0f;
        attn_mask[i] = keep ? 0.0f : -INFINITY;
    }
    std::vector<float> zeros(H, 0.0f);
    std::vector<int32_t> spk_index(1, std::max(0, in.spk_id));

    ggml_init_params ip_in { ggml_tensor_overhead() * 16, nullptr, true };
    ggml_context * ctx_in = ggml_init(ip_in);
    ggml_tensor * tok_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_I32, L);
    ggml_set_input(tok_t); ggml_set_name(tok_t, "tok");
    ggml_tensor * dur_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, 1, L);
    ggml_set_input(dur_t); ggml_set_name(dur_t, "dur");
    ggml_tensor * mel2ph_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_I32, T);
    ggml_set_input(mel2ph_t); ggml_set_name(mel2ph_t, "mel2ph");
    ggml_tensor * nonpad_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, 1, L);
    ggml_set_input(nonpad_t); ggml_set_name(nonpad_t, "nonpad");
    ggml_tensor * attn_mask_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_F32, L);
    ggml_set_input(attn_mask_t); ggml_set_name(attn_mask_t, "attn_mask");
    ggml_tensor * pos_t = nullptr;
    if (c.use_rope) {
        pos_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_I32, L);
        ggml_set_input(pos_t); ggml_set_name(pos_t, "pos");
    }
    ggml_tensor * zero_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, H, 1);
    ggml_set_input(zero_t); ggml_set_name(zero_t, "zero");
    ggml_tensor * lang_t = nullptr;
    if (c.use_lang_id) {
        lang_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_I32, L);
        ggml_set_input(lang_t); ggml_set_name(lang_t, "lang");
    }
    ggml_tensor * spk_t = nullptr;
    if (c.use_spk_id && in.spk_id >= 0) {
        spk_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_I32, 1);
        ggml_set_input(spk_t); ggml_set_name(spk_t, "spk");
    }

    ggml_init_params ip { 768ULL * 1024 * 1024, nullptr, true };
    ggml_context * ctx = ggml_init(ip);
    ggml_cgraph * gf = ggml_new_graph_custom(ctx, GGML_DEFAULT_GRAPH_SIZE * 64, false);

    // Pitch FS2 uses ph_dur_embed (not onset_embed + word_dur_embed)
    ggml_tensor * main = ggml_get_rows(ctx, m.get("fs2.txt_embed.weight"), tok_t);
    ggml_tensor * extra = linear(ctx, m, "fs2.ph_dur_embed", dur_t);
    if (c.use_lang_id) {
        extra = ggml_add(ctx, extra, ggml_get_rows(ctx, m.get("fs2.lang_embed.weight"), lang_t));
    }

    ggml_tensor * enc = run_encoder_graph(ctx, m, "fs2", main, extra, nonpad_t, attn_mask_t, pos_t,
                                          (int)c.enc_layers, H, (int)c.num_heads,
                                          (int)c.enc_ffn_kernel_size, c.ffn_act,
                                          c.use_rope, c.rope_interleaved);
    // Pad encoder [L, H] → [L+1, H] with zeros at idx 0, then gather by mel2ph
    ggml_tensor * cond = ggml_get_rows(ctx, ggml_concat(ctx, zero_t, enc, 1), mel2ph_t);
    if (c.use_spk_id && in.spk_id >= 0) {
        cond = ggml_add(ctx, cond, ggml_get_rows(ctx, m.get("spk_embed.weight"), spk_t));
    }
    ggml_set_output(cond);
    ggml_set_name(cond, "fs2_condition");
    ggml_build_forward_expand(gf, cond);

    ggml_backend_buffer_t in_buf = ggml_backend_alloc_ctx_tensors(ctx_in, m.backend);
    if (!in_buf) {
        ggml_free(ctx); ggml_free(ctx_in);
        return false;
    }
    ggml_gallocr_t ga = ggml_gallocr_new(ggml_backend_get_default_buffer_type(m.backend));
    if (!ggml_gallocr_alloc_graph(ga, gf)) {
        ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
        ggml_free(ctx); ggml_free(ctx_in);
        return false;
    }

    ggml_backend_tensor_set(tok_t, in.tokens.data(), 0, L * sizeof(int32_t));
    ggml_backend_tensor_set(dur_t, ph_dur_scaled.data(), 0, L * sizeof(float));
    ggml_backend_tensor_set(mel2ph_t, in.mel2ph.data(), 0, T * sizeof(int32_t));
    ggml_backend_tensor_set(nonpad_t, nonpad.data(), 0, L * sizeof(float));
    ggml_backend_tensor_set(attn_mask_t, attn_mask.data(), 0, L * sizeof(float));
    if (pos_t) {
        std::vector<int32_t> positions(L);
        for (int i = 0; i < L; ++i) positions[i] = i;
        ggml_backend_tensor_set(pos_t, positions.data(), 0, L * sizeof(int32_t));
    }
    ggml_backend_tensor_set(zero_t, zeros.data(), 0, H * sizeof(float));
    if (lang_t) {
        ggml_backend_tensor_set(lang_t, in.languages.data(), 0, L * sizeof(int32_t));
    }
    if (spk_t) {
        ggml_backend_tensor_set(spk_t, spk_index.data(), 0, sizeof(int32_t));
    }

    ggml_status st = ggml_backend_graph_compute(m.backend, gf);
    if (st != GGML_STATUS_SUCCESS) {
        fprintf(stderr, "[fatal] pitch fs2 compute = %d\n", (int)st);
        ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
        ggml_free(ctx); ggml_free(ctx_in);
        return false;
    }

    condition_out.resize((size_t)T * H);
    ggml_backend_tensor_get(cond, condition_out.data(), 0, condition_out.size() * sizeof(float));

    ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
    ggml_free(ctx); ggml_free(ctx_in);
    return true;
}

// ---------------------------------------------------------------------------
// Melody Encoder
// ---------------------------------------------------------------------------
static bool run_melody_encoder(const Model & m, const PitchInputs & in,
                               std::vector<float> & melody_out) {
    const Config & c = m.cfg;
    const int N = in.notes;
    const int T = in.frames;
    const int mel_H = (int)c.melody_hidden_size;
    const int out_H = (int)c.hidden_size;

    // Prepare note inputs
    std::vector<float> midi_input(N);
    std::vector<float> dur_input(N);
    std::vector<float> nonpad(N);
    std::vector<float> attn_mask(N);
    for (int i = 0; i < N; ++i) {
        float midi = in.note_midi[i];
        bool is_pad = (midi < 0.0f);
        bool is_rest = (in.note_rest[i] != 0);
        if (c.use_variance_scaling) {
            midi_input[i] = is_rest ? 0.0f : (midi / 128.0f);
            dur_input[i] = std::log(1.0f + (float)in.note_dur[i]);
        } else {
            midi_input[i] = is_rest ? 0.0f : midi;
            dur_input[i] = (float)in.note_dur[i];
        }
        nonpad[i] = is_pad ? 0.0f : 1.0f;
        attn_mask[i] = is_pad ? -INFINITY : 0.0f;
    }

    ggml_init_params ip_in { ggml_tensor_overhead() * 12, nullptr, true };
    ggml_context * ctx_in = ggml_init(ip_in);
    ggml_tensor * midi_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, 1, N);
    ggml_set_input(midi_t); ggml_set_name(midi_t, "midi");
    ggml_tensor * dur_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, 1, N);
    ggml_set_input(dur_t); ggml_set_name(dur_t, "dur");
    ggml_tensor * nonpad_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, 1, N);
    ggml_set_input(nonpad_t); ggml_set_name(nonpad_t, "mel_nonpad");
    ggml_tensor * attn_mask_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_F32, N);
    ggml_set_input(attn_mask_t); ggml_set_name(attn_mask_t, "mel_attn_mask");
    ggml_tensor * pos_t = nullptr;
    if (c.use_rope) {
        pos_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_I32, N);
        ggml_set_input(pos_t); ggml_set_name(pos_t, "mel_pos");
    }
    ggml_tensor * mel2note_t = ggml_new_tensor_1d(ctx_in, GGML_TYPE_I32, T);
    ggml_set_input(mel2note_t); ggml_set_name(mel2note_t, "mel2note");
    // Zero row for mel2note padding (index 0)
    ggml_tensor * zero_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, out_H, 1);
    ggml_set_input(zero_t); ggml_set_name(zero_t, "mel_zero");

    ggml_init_params ip { 768ULL * 1024 * 1024, nullptr, true };
    ggml_context * ctx = ggml_init(ip);
    ggml_cgraph * gf = ggml_new_graph_custom(ctx, GGML_DEFAULT_GRAPH_SIZE * 64, false);

    // note_midi_embed(midi) * ~rest_mask
    ggml_tensor * midi_emb = linear(ctx, m, "mel.midi_embed", midi_t);
    // Apply rest mask: midi_embed[i] = 0 if rest
    midi_emb = ggml_mul(ctx, midi_emb, nonpad_t);  // nonpad also masks rest (we set midi_input=0 for rest)

    // note_dur_embed(dur)
    ggml_tensor * dur_emb = linear(ctx, m, "mel.dur_embed", dur_t);

    // Run melody encoder transformer
    ggml_tensor * enc = run_encoder_graph(ctx, m, "mel", midi_emb, dur_emb, nonpad_t, attn_mask_t, pos_t,
                                          (int)c.melody_enc_layers, mel_H, (int)c.melody_num_heads,
                                          (int)c.melody_ffn_kernel_size, c.ffn_act,
                                          c.use_rope, c.rope_interleaved);

    // out_proj: [N, mel_H] → [N, H]
    enc = linear(ctx, m, "mel.out_proj", enc);

    // Pad [N, H] → [N+1, H], gather by mel2note → [T, H]
    ggml_tensor * gathered = ggml_get_rows(ctx, ggml_concat(ctx, zero_t, enc, 1), mel2note_t);
    ggml_set_output(gathered);
    ggml_set_name(gathered, "melody_condition");
    ggml_build_forward_expand(gf, gathered);

    ggml_backend_buffer_t in_buf = ggml_backend_alloc_ctx_tensors(ctx_in, m.backend);
    if (!in_buf) {
        ggml_free(ctx); ggml_free(ctx_in);
        return false;
    }
    ggml_gallocr_t ga = ggml_gallocr_new(ggml_backend_get_default_buffer_type(m.backend));
    if (!ggml_gallocr_alloc_graph(ga, gf)) {
        ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
        ggml_free(ctx); ggml_free(ctx_in);
        return false;
    }

    ggml_backend_tensor_set(midi_t, midi_input.data(), 0, N * sizeof(float));
    ggml_backend_tensor_set(dur_t, dur_input.data(), 0, N * sizeof(float));
    ggml_backend_tensor_set(nonpad_t, nonpad.data(), 0, N * sizeof(float));
    ggml_backend_tensor_set(attn_mask_t, attn_mask.data(), 0, N * sizeof(float));
    if (pos_t) {
        std::vector<int32_t> positions(N);
        for (int i = 0; i < N; ++i) positions[i] = i;
        ggml_backend_tensor_set(pos_t, positions.data(), 0, N * sizeof(int32_t));
    }
    ggml_backend_tensor_set(mel2note_t, in.mel2note.data(), 0, T * sizeof(int32_t));
    std::vector<float> zero_vec(out_H, 0.0f);
    ggml_backend_tensor_set(zero_t, zero_vec.data(), 0, out_H * sizeof(float));

    ggml_status st = ggml_backend_graph_compute(m.backend, gf);
    if (st != GGML_STATUS_SUCCESS) {
        fprintf(stderr, "[fatal] melody encoder compute = %d\n", (int)st);
        ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
        ggml_free(ctx); ggml_free(ctx_in);
        return false;
    }

    melody_out.resize((size_t)T * out_H);
    ggml_backend_tensor_get(gathered, melody_out.data(), 0, melody_out.size() * sizeof(float));

    ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
    ggml_free(ctx); ggml_free(ctx_in);
    return true;
}

// ---------------------------------------------------------------------------
// Compose pitch condition
// ---------------------------------------------------------------------------
static bool compose_pitch_condition(const Model & m,
                                    const PitchInputs & in,
                                    const std::vector<float> & fs2_cond,
                                    const std::vector<float> & melody_cond,
                                    std::vector<float> & pitch_cond_out) {
    const Config & c = m.cfg;
    const int T = in.frames;
    const int H = (int)c.hidden_size;

    // pitch_cond = fs2_cond + melody_cond + retake_embed(1) + delta_pitch_embed(0)
    // retake_embed: row 1 of [2, H] embedding (full retake during inference)
    // delta_pitch_embed: Linear(0) = bias only (input is all zeros)

    ggml_init_params ip_in { ggml_tensor_overhead() * 4, nullptr, true };
    ggml_context * ctx_in = ggml_init(ip_in);
    ggml_tensor * fs2_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, H, T);
    ggml_set_input(fs2_t); ggml_set_name(fs2_t, "fs2");
    ggml_tensor * mel_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, H, T);
    ggml_set_input(mel_t); ggml_set_name(mel_t, "mel");
    // Retake index: all 1s (retake = regenerate)
    ggml_tensor * retake_idx = ggml_new_tensor_1d(ctx_in, GGML_TYPE_I32, T);
    ggml_set_input(retake_idx); ggml_set_name(retake_idx, "retake");

    ggml_init_params ip { 128ULL * 1024 * 1024, nullptr, true };
    ggml_context * ctx = ggml_init(ip);
    ggml_cgraph * gf = ggml_new_graph_custom(ctx, GGML_DEFAULT_GRAPH_SIZE * 4, false);

    ggml_tensor * cond = ggml_add(ctx, fs2_t, mel_t);
    // pitch_retake_embed(1) for all frames
    ggml_tensor * retake_emb = ggml_get_rows(ctx, m.get("pitch_retake_embed.weight"), retake_idx);
    cond = ggml_add(ctx, cond, retake_emb);
    // delta_pitch_embed(0): just the bias since input is 0
    // But to be correct through the graph, we add the bias directly
    cond = ggml_add(ctx, cond, m.get("delta_pitch_embed.bias"));
    ggml_set_output(cond);
    ggml_set_name(cond, "pitch_cond");
    ggml_build_forward_expand(gf, cond);

    ggml_backend_buffer_t in_buf = ggml_backend_alloc_ctx_tensors(ctx_in, m.backend);
    if (!in_buf) {
        ggml_free(ctx); ggml_free(ctx_in);
        return false;
    }
    ggml_gallocr_t ga = ggml_gallocr_new(ggml_backend_get_default_buffer_type(m.backend));
    if (!ggml_gallocr_alloc_graph(ga, gf)) {
        ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
        ggml_free(ctx); ggml_free(ctx_in);
        return false;
    }

    ggml_backend_tensor_set(fs2_t, fs2_cond.data(), 0, fs2_cond.size() * sizeof(float));
    ggml_backend_tensor_set(mel_t, melody_cond.data(), 0, melody_cond.size() * sizeof(float));
    std::vector<int32_t> retake_ones(T, 1);
    ggml_backend_tensor_set(retake_idx, retake_ones.data(), 0, T * sizeof(int32_t));

    ggml_status st = ggml_backend_graph_compute(m.backend, gf);
    if (st != GGML_STATUS_SUCCESS) {
        fprintf(stderr, "[fatal] pitch condition compose = %d\n", (int)st);
        ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
        ggml_free(ctx); ggml_free(ctx_in);
        return false;
    }

    pitch_cond_out.resize((size_t)T * H);
    ggml_backend_tensor_get(cond, pitch_cond_out.data(), 0, pitch_cond_out.size() * sizeof(float));

    ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
    ggml_free(ctx); ggml_free(ctx_in);
    return true;
}

// ---------------------------------------------------------------------------
// Compose variance condition (for voicing prediction)
// ---------------------------------------------------------------------------
static bool compose_variance_condition(const Model & m,
                                       const std::vector<float> & fs2_cond,
                                       const std::vector<float> & pitch_midi,
                                       int T, int H,
                                       std::vector<float> & var_cond_out) {
    const Config & c = m.cfg;

    std::vector<float> pitch_input(T);
    for (int i = 0; i < T; ++i) {
        pitch_input[i] = c.use_variance_scaling ? (pitch_midi[i] / 12.0f) : pitch_midi[i];
    }

    ggml_init_params ip_in { ggml_tensor_overhead() * 4, nullptr, true };
    ggml_context * ctx_in = ggml_init(ip_in);
    ggml_tensor * fs2_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, H, T);
    ggml_set_input(fs2_t); ggml_set_name(fs2_t, "fs2");
    ggml_tensor * pitch_t = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, 1, T);
    ggml_set_input(pitch_t); ggml_set_name(pitch_t, "pitch");

    ggml_init_params ip { 64ULL * 1024 * 1024, nullptr, true };
    ggml_context * ctx = ggml_init(ip);
    ggml_cgraph * gf = ggml_new_graph_custom(ctx, GGML_DEFAULT_GRAPH_SIZE * 4, false);

    ggml_tensor * emb = ggml_add(ctx,
        ggml_mul_mat(ctx, m.get("pitch_embed.weight"), pitch_t),
        m.get("pitch_embed.bias"));
    ggml_tensor * cond = ggml_add(ctx, fs2_t, emb);
    ggml_set_output(cond); ggml_set_name(cond, "var_cond");
    ggml_build_forward_expand(gf, cond);

    ggml_backend_buffer_t in_buf = ggml_backend_alloc_ctx_tensors(ctx_in, m.backend);
    if (!in_buf) { ggml_free(ctx); ggml_free(ctx_in); return false; }
    ggml_gallocr_t ga = ggml_gallocr_new(ggml_backend_get_default_buffer_type(m.backend));
    if (!ggml_gallocr_alloc_graph(ga, gf)) {
        ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
        ggml_free(ctx); ggml_free(ctx_in); return false;
    }

    ggml_backend_tensor_set(fs2_t, fs2_cond.data(), 0, fs2_cond.size() * sizeof(float));
    ggml_backend_tensor_set(pitch_t, pitch_input.data(), 0, T * sizeof(float));
    ggml_status st = ggml_backend_graph_compute(m.backend, gf);
    if (st != GGML_STATUS_SUCCESS) {
        ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
        ggml_free(ctx); ggml_free(ctx_in); return false;
    }

    var_cond_out.resize((size_t)T * H);
    ggml_backend_tensor_get(cond, var_cond_out.data(), 0, var_cond_out.size() * sizeof(float));

    ggml_gallocr_free(ga); ggml_backend_buffer_free(in_buf);
    ggml_free(ctx); ggml_free(ctx_in);
    return true;
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------
bool run_pitch_inference(const Model & m, const PitchInputs & in, PitchOutputs & out) {
    const Config & c = m.cfg;
    const int T = in.frames;
    const int H = (int)c.hidden_size;

    if (T <= 0 || in.phones <= 0) {
        fprintf(stderr, "[err] pitch: frames and phones must be positive\n");
        return false;
    }
    if (!c.use_melody_encoder) {
        fprintf(stderr, "[err] pitch: only use_melody_encoder=true is supported\n");
        return false;
    }

    // 1. FS2 encoder → condition [T, H]
    std::vector<float> fs2_cond;
    if (!run_fs2_encoder(m, in, fs2_cond)) return false;

    // 2. Melody encoder → [T, H]
    std::vector<float> melody_cond;
    if (!run_melody_encoder(m, in, melody_cond)) return false;

    // 3. Compose pitch condition
    std::vector<float> pitch_cond;
    if (!compose_pitch_condition(m, in, fs2_cond, melody_cond, pitch_cond)) return false;

    // 4. Pitch reflow sampling
    const int R = (int)c.pitch.repeat_bins;
    const int steps = std::max(1u, c.sampling_steps);
    const float dt = 1.0f / (float)steps;
    const std::string & alg = c.sampling_algorithm;

    std::mt19937 rng(in.seed);
    std::normal_distribution<float> norm(0.0f, 1.0f);
    std::vector<float> x((size_t)T * R);
    for (float & v : x) v = norm(rng);

    // Load diagnostic noise if provided
    if (const char * np = std::getenv("DSDIAG_PITCH_NOISE")) {
        FILE * fp = std::fopen(np, "rb");
        if (fp) {
            std::fseek(fp, 0, SEEK_END);
            long sz = std::ftell(fp);
            std::fseek(fp, 0, SEEK_SET);
            if ((size_t)sz == x.size() * sizeof(float)) {
                std::fread(x.data(), sizeof(float), x.size(), fp);
                fprintf(stderr, "[debug] loaded pitch noise from %s\n", np);
            }
            std::fclose(fp);
        }
    }

    auto eval_pitch_velocity = [&](const std::vector<float> & x_eval, float t_eval,
                                    std::vector<float> & v_out) -> bool {
        float diff_step = (float)c.time_scale_factor * t_eval;
        return run_velocity(m, "pitch", c.pitch, T, R, diff_step, x_eval, pitch_cond, H, v_out);
    };

    for (int i = 0; i < steps; ++i) {
        float t_i = (float)i * dt;
        if (alg == "midpoint" || alg == "rk2") {
            std::vector<float> v1;
            if (!eval_pitch_velocity(x, t_i, v1)) return false;
            std::vector<float> x_mid(x.size());
            for (size_t j = 0; j < x.size(); ++j)
                x_mid[j] = x[j] + 0.5f * dt * v1[j];
            std::vector<float> v2;
            if (!eval_pitch_velocity(x_mid, t_i + 0.5f * dt, v2)) return false;
            for (size_t j = 0; j < x.size(); ++j)
                x[j] += dt * v2[j];
        } else if (alg == "rk4") {
            std::vector<float> k1, k2, k3, k4;
            if (!eval_pitch_velocity(x, t_i, k1)) return false;
            std::vector<float> x_tmp(x.size());
            for (size_t j = 0; j < x.size(); ++j) x_tmp[j] = x[j] + 0.5f * dt * k1[j];
            if (!eval_pitch_velocity(x_tmp, t_i + 0.5f * dt, k2)) return false;
            for (size_t j = 0; j < x.size(); ++j) x_tmp[j] = x[j] + 0.5f * dt * k2[j];
            if (!eval_pitch_velocity(x_tmp, t_i + 0.5f * dt, k3)) return false;
            for (size_t j = 0; j < x.size(); ++j) x_tmp[j] = x[j] + dt * k3[j];
            if (!eval_pitch_velocity(x_tmp, t_i + dt, k4)) return false;
            for (size_t j = 0; j < x.size(); ++j)
                x[j] += dt / 6.0f * (k1[j] + 2.f*k2[j] + 2.f*k3[j] + k4[j]);
        } else {
            // Euler
            std::vector<float> v1;
            if (!eval_pitch_velocity(x, t_i, v1)) return false;
            for (size_t j = 0; j < x.size(); ++j)
                x[j] += dt * v1[j];
        }
    }

    // 5. Denormalize pitch delta: mean over repeat_bins, then denorm + clip
    const float norm_min = c.pitch.pitd_norm_min;
    const float norm_max = c.pitch.pitd_norm_max;
    const float clip_min = c.pitch.pitd_clip_min;
    const float clip_max = c.pitch.pitd_clip_max;

    std::vector<float> pitch_delta(T);
    for (int t = 0; t < T; ++t) {
        float sum = 0.0f;
        for (int r = 0; r < R; ++r) {
            float v = x[(size_t)t * R + r];
            v = (v + 1.0f) * 0.5f * (norm_max - norm_min) + norm_min;
            v = std::max(clip_min, std::min(clip_max, v));
            sum += v;
        }
        pitch_delta[t] = sum / (float)R;
    }

    // 6. Final pitch = base_pitch + pitch_delta → Hz
    out.frames = T;
    out.pitch_midi.resize(T);
    out.f0_hz.resize(T);
    for (int t = 0; t < T; ++t) {
        float midi = in.base_pitch[t] + pitch_delta[t];
        out.pitch_midi[t] = midi;
        if (in.base_pitch[t] > 0.0f) {
            out.f0_hz[t] = 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
        } else {
            out.f0_hz[t] = 0.0f;  // rest
        }
    }

    // 7. Optional: predict voicing
    if (c.predict_voicing) {
        std::vector<float> var_cond;
        if (!compose_variance_condition(m, fs2_cond, out.pitch_midi, T, H, var_cond)) return false;

        const int var_R = (int)c.variance.total_repeat_bins;
        const int var_steps = steps;  // reuse same step count

        std::vector<float> vx((size_t)T * var_R);
        for (float & v : vx) v = norm(rng);

        auto eval_var_velocity = [&](const std::vector<float> & x_eval, float t_eval,
                                      std::vector<float> & v_out) -> bool {
            float diff_step = (float)c.time_scale_factor * t_eval;
            return run_velocity(m, "var", c.variance, T, var_R, diff_step, x_eval, var_cond, H, v_out);
        };

        for (int i = 0; i < var_steps; ++i) {
            float t_i = (float)i * dt;
            if (alg == "midpoint" || alg == "rk2") {
                std::vector<float> v1;
                if (!eval_var_velocity(vx, t_i, v1)) return false;
                std::vector<float> x_mid(vx.size());
                for (size_t j = 0; j < vx.size(); ++j) x_mid[j] = vx[j] + 0.5f * dt * v1[j];
                std::vector<float> v2;
                if (!eval_var_velocity(x_mid, t_i + 0.5f * dt, v2)) return false;
                for (size_t j = 0; j < vx.size(); ++j) vx[j] += dt * v2[j];
            } else if (alg == "rk4") {
                std::vector<float> k1, k2, k3, k4;
                if (!eval_var_velocity(vx, t_i, k1)) return false;
                std::vector<float> x_tmp(vx.size());
                for (size_t j = 0; j < vx.size(); ++j) x_tmp[j] = vx[j] + 0.5f * dt * k1[j];
                if (!eval_var_velocity(x_tmp, t_i + 0.5f * dt, k2)) return false;
                for (size_t j = 0; j < vx.size(); ++j) x_tmp[j] = vx[j] + 0.5f * dt * k2[j];
                if (!eval_var_velocity(x_tmp, t_i + 0.5f * dt, k3)) return false;
                for (size_t j = 0; j < vx.size(); ++j) x_tmp[j] = vx[j] + dt * k3[j];
                if (!eval_var_velocity(x_tmp, t_i + dt, k4)) return false;
                for (size_t j = 0; j < vx.size(); ++j)
                    vx[j] += dt / 6.0f * (k1[j] + 2.f*k2[j] + 2.f*k3[j] + k4[j]);
            } else {
                std::vector<float> v1;
                if (!eval_var_velocity(vx, t_i, v1)) return false;
                for (size_t j = 0; j < vx.size(); ++j) vx[j] += dt * v1[j];
            }
        }

        // Denormalize voicing (single target, all bins are voicing)
        const int per_R = var_R;  // total_repeat_bins = per_R * 1 target
        out.voicing.resize(T);
        for (int t = 0; t < T; ++t) {
            float sum = 0.0f;
            for (int r = 0; r < per_R; ++r) {
                float v = vx[(size_t)t * var_R + r];
                v = (v + 1.0f) * 0.5f * (c.variance.voicing_norm_max - c.variance.voicing_norm_min)
                    + c.variance.voicing_norm_min;
                v = std::max(c.variance.voicing_clip_min, std::min(c.variance.voicing_clip_max, v));
                sum += v;
            }
            out.voicing[t] = sum / (float)per_R;
        }
    }

    return true;
}

} // namespace dsp_pitch
