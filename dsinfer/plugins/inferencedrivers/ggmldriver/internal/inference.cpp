// Full FastSpeech2 encoder + LYNXNet velocity + Rectified Flow Euler sampler.
//
// Flow:
//   1. encoder graph: txt/dur/lang embed -> RelPosEnc -> 4x Transformer
//      -> final LN -> length regulate (mel2ph) -> +pitch/variance/key/speed
//      -> cond [H, T]  (cached to host)
//   2. backbone graph (single Euler step, reused N times):
//      x_t [M, T], t_scalar -> velocity_fn(x_t, t*1000, cond) -> v [M, T]
//   3. host loop: for i in [0, steps):  x += dt * v(x, t_start + i*dt)
//   4. denorm_spec on host

#include "diffsinger.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace ds {

static std::vector<int32_t> build_mel2ph(const std::vector<int32_t> & dur, int & T) {
    T = 0;
    for (int d : dur) T += d;
    std::vector<int32_t> m(T);
    int idx = 0;
    for (int i = 0; i < (int)dur.size(); ++i)
        for (int k = 0; k < dur[i]; ++k)
            m[idx++] = i + 1;          // 1-based; 0 is the zero-pad row
    return m;
}

static std::string tn(const char * fmt, int i) {
    char buf[128]; snprintf(buf, sizeof buf, fmt, i); return buf;
}

static bool is_mix_ln_layer(const Config & c, int li) {
    for (uint32_t v : c.mix_ln_layers) {
        if ((int)v == li) return true;
    }
    return false;
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
    // out * atan(gate) using branchless polynomial approximation that runs
    // on Metal/CPU/CUDA via stock ggml ops.
    //   sx = sign(gate)
    //   ax = |gate|
    //   big = step(ax - 1)   (1 if ax > 1 else 0)
    //   y   = ax/(1+ax) when ax small, 1/ax when ax large — but a simpler
    //         branchless choice that keeps |y| <= 1 is:
    //           y = ax        when ax <= 1
    //           y = 1/ax      when ax >  1
    //         which we implement as y = (1-big)*ax + big*(1/(ax+ε))
    //   poly(y) = y*(0.999866 + y²*(-0.330299 + y²*(0.180141
    //              + y²*(-0.085133 + y²*0.020835))))
    //   atan(x) = sx * ( (1-big)*poly(y) + big*(π/2 - poly(y)) )
    //   atanglu = out * atan(gate)
    const float pi_half = 1.5707963267948966f;
    ggml_tensor * sx  = ggml_sgn(ctx, gate);
    ggml_tensor * ax  = ggml_abs(ctx, gate);
    ggml_tensor * eps = ggml_scale_bias(ctx, ax, 1.0f, 1e-12f);   // ax + ε
    ggml_tensor * ones = ggml_scale_bias(ctx, ax, 0.0f, 1.0f);
    ggml_tensor * recip = ggml_div(ctx, ones, eps);                // 1/(ax+ε)
    // big = step(ax - 1)
    ggml_tensor * ax_m1 = ggml_scale_bias(ctx, ax, 1.0f, -1.0f);   // ax - 1
    ggml_tensor * big   = ggml_step(ctx, ax_m1);                   // 1 if ax>1 else 0
    ggml_tensor * sml   = ggml_scale_bias(ctx, big, -1.0f, 1.0f);  // 1 - big
    // y = sml*ax + big*recip
    ggml_tensor * y = ggml_add(ctx,
        ggml_mul(ctx, sml, ax),
        ggml_mul(ctx, big, recip));
    // poly(y)
    ggml_tensor * y2 = ggml_mul(ctx, y, y);
    auto fma = [&](ggml_tensor * a, ggml_tensor * b, float c_) {
        // a*b + c_*ones (= a*b + c_)
        return ggml_scale_bias(ctx, ggml_mul(ctx, a, b), 1.0f, c_);
    };
    ggml_tensor * p =                               // 0.020835
        ggml_scale_bias(ctx, y2, 0.0f,  0.020835f);
    p = fma(p, y2, -0.085133f);
    p = fma(p, y2,  0.180141f);
    p = fma(p, y2, -0.330299f);
    p = fma(p, y2,  0.999866f);
    ggml_tensor * poly = ggml_mul(ctx, p, y);
    // atan_pos = sml*poly + big*(π/2 - poly)
    ggml_tensor * neg_poly = ggml_scale_bias(ctx, poly, -1.0f,  pi_half);
    ggml_tensor * atan_pos = ggml_add(ctx,
        ggml_mul(ctx, sml, poly),
        ggml_mul(ctx, big, neg_poly));
    ggml_tensor * atan_g = ggml_mul(ctx, sx, atan_pos);
    return ggml_mul(ctx, out, atan_g);
}


static ggml_tensor * acoustic_layer_norm(ggml_context * ctx,
                                         const Model & m,
                                         const Config & c,
                                         ggml_tensor * x,
                                         ggml_tensor * spk_embed,
                                         int li,
                                         int ln_idx,
                                         int H) {
    ggml_tensor * h = ggml_norm(ctx, x, 1e-5f);
    if (c.use_mix_ln && is_mix_ln_layer(c, li)) {
        if (!spk_embed) {
            fprintf(stderr, "[fatal] mix layer norm requires speaker embedding\n");
            std::abort();
        }
        std::string p = tn("fs2.enc.%d.ln", li) + std::to_string(ln_idx) + ".affine";
        ggml_tensor * affine = linear(ctx, m, p, spk_embed);
        // PyTorch: betas, gammas = split(affine, C, dim=-1); return gammas*x + betas
        // So first half = betas (offset), second half = gammas (scale).
        // Checkpoint bias confirms: first half ≈ 1, second half ≈ 0 at init.
        ggml_tensor * betas  = ggml_view_2d(ctx, affine, H, 1, affine->nb[1], 0);
        ggml_tensor * gammas = ggml_view_2d(ctx, affine, H, 1, affine->nb[1], H * affine->nb[0]);
        return ggml_add(ctx, ggml_mul(ctx, h, gammas), betas);
    }
    return ggml_add(ctx,
        ggml_mul(ctx, h, m.get(tn("fs2.enc.%d.ln", li) + std::to_string(ln_idx) + ".weight")),
        m.get(tn("fs2.enc.%d.ln", li) + std::to_string(ln_idx) + ".bias"));
}

// Run the optional ConvNeXt aux_decoder on a host-side `cond_host` of shape
// [H, T] (memory layout = T rows of H floats), producing aux_mel of shape
// [T, M] in `out_mel` (post spec_min/max denorm — same units as the main mel).
// Returns false if the model has no aux decoder.
static bool run_aux_decoder(const Model & m, const std::vector<float> & cond_host,
                            int T, int H, int M,
                            std::vector<float> & out_mel) {
    const Config & c = m.cfg;
    if (!c.has_aux_decoder) return false;
    const int K = (int)c.aux_kernel_size;
    const int NL = (int)c.aux_num_layers;

    // Inputs in a separate ctx + persistent buffer (same gallocr aliasing fix
    // we use elsewhere — input must not share storage with intermediates).
    ggml_init_params ip_in { ggml_tensor_overhead() * 8, nullptr, /*no_alloc*/ true };
    ggml_context * ctx_in = ggml_init(ip_in);
    ggml_init_params ip { 256ULL * 1024 * 1024, nullptr, /*no_alloc*/ true };
    ggml_context * ctx = ggml_init(ip);
    ggml_cgraph * gf = ggml_new_graph_custom(ctx, GGML_DEFAULT_GRAPH_SIZE * 8, false);

    ggml_tensor * c_in = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, H, T);
    ggml_set_input(c_in); ggml_set_name(c_in, "aux_c_in");

    // ---- inconv: Conv1d(H, C, k=K, padding=K/2) ----
    // Same depthwise/pointwise dance as the main backbone: data must be
    // [T, IC] (channel-last) with FP16 kernel for ggml_conv_1d.
    auto convkx = [&](ggml_tensor * w, ggml_tensor * b, ggml_tensor * x) {
        ggml_tensor * w16 = ggml_cast(ctx, w, GGML_TYPE_F16);
        ggml_tensor * x_tcl = ggml_cont(ctx, ggml_transpose(ctx, x));
        ggml_tensor * y_tcl = ggml_conv_1d(ctx, w16, x_tcl, /*s*/1, /*p*/K/2, /*d*/1);
        ggml_tensor * y = ggml_cont(ctx, ggml_transpose(ctx, y_tcl));
        if (b) y = ggml_add(ctx, y, b);
        return y;
    };
    ggml_tensor * x = convkx(m.get("aux.input_proj.weight"),
                             m.get("aux.input_proj.bias"), c_in);  // [C, T]

    // Optional debug tap (DSDIAG_AUX_TAP=after_inconv|after_dw0|after_layer0|...)
    const char * aux_tap = std::getenv("DSDIAG_AUX_TAP");
    ggml_tensor * tap_t = nullptr;
    if (aux_tap && std::strcmp(aux_tap, "after_inconv") == 0) tap_t = x;

    // ---- 6 ConvNeXt blocks ----
    for (int li = 0; li < NL; ++li) {
        ggml_tensor * res = x;

        // depthwise Conv1d(C, C, k=K, pad=K/2, groups=C)
        ggml_tensor * dw = m.get(tn("aux.layer.%d.dw.weight", li));
        ggml_tensor * dw16 = ggml_cast(ctx, dw, GGML_TYPE_F16);
        ggml_tensor * x_tcl = ggml_cont(ctx, ggml_transpose(ctx, x));    // [T, C]
        ggml_tensor * y_dw  = ggml_conv_1d_dw(ctx, dw16, x_tcl, /*s*/1, /*p*/K/2, /*d*/1);
        x = ggml_cont(ctx, ggml_transpose(ctx, y_dw));                    // [C, T]
        x = ggml_add(ctx, x, m.get(tn("aux.layer.%d.dw.bias", li)));
        if (aux_tap && li == 0 && std::strcmp(aux_tap, "after_dw0") == 0) tap_t = x;

        // LayerNorm over channel dim (eps=1e-6 in convnext.py:27)
        x = ggml_norm(ctx, x, 1e-6f);
        x = ggml_add(ctx,
            ggml_mul(ctx, x, m.get(tn("aux.layer.%d.ln.weight", li))),
            m.get(tn("aux.layer.%d.ln.bias", li)));
        if (aux_tap && li == 0 && std::strcmp(aux_tap, "after_ln0") == 0) tap_t = x;

        // pwconv1: Linear(C, 4C). Weight is stored as PyTorch [out=4C, in=C].
        // mul_mat(W[in, out], x[in, T]) -> [out, T] — matches our layout.
        ggml_tensor * w1 = m.get(tn("aux.layer.%d.pw1.weight", li));     // [4C, C]
        // ggml mul_mat with W[K, N] * x[K, T] -> [N, T]; we need W stored as
        // ne=[in, out] = ne=[C, 4C]. The PyTorch [out, in]=[4C, C] flat is
        // ne=[C, 4C] in ggml's reversed-shape ordering. So we can use it as-is.
        x = ggml_add(ctx, ggml_mul_mat(ctx, w1, x),
                     m.get(tn("aux.layer.%d.pw1.bias", li)));            // [4C, T]
        if (aux_tap && li == 0 && std::strcmp(aux_tap, "after_pw1_0") == 0) tap_t = x;
        x = ggml_gelu_erf(ctx, x);
        if (aux_tap && li == 0 && std::strcmp(aux_tap, "after_act_0") == 0) tap_t = x;
        ggml_tensor * w2 = m.get(tn("aux.layer.%d.pw2.weight", li));     // [C, 4C] PyTorch -> ne=[4C, C]
        x = ggml_add(ctx, ggml_mul_mat(ctx, w2, x),
                     m.get(tn("aux.layer.%d.pw2.bias", li)));            // [C, T]
        if (aux_tap && li == 0 && std::strcmp(aux_tap, "after_pw2_0") == 0) tap_t = x;

        // gamma scale (per-channel)
        ggml_tensor * gamma = m.get(tn("aux.layer.%d.gamma", li), /*optional*/true);
        if (gamma) {
            x = ggml_mul(ctx, x, gamma);
        }
        x = ggml_add(ctx, x, res);
        if (aux_tap && std::strcmp(aux_tap, (std::string("after_layer") + std::to_string(li)).c_str()) == 0) tap_t = x;
    }

    // ---- outconv: Conv1d(C, M, k=K, padding=K/2) ----
    ggml_tensor * out_t = convkx(m.get("aux.output_proj.weight"),
                                 m.get("aux.output_proj.bias"), x);     // [M, T]
    if (tap_t) out_t = ggml_cont(ctx, tap_t);

    ggml_set_output(out_t); ggml_set_name(out_t, "aux_out");
    ggml_build_forward_expand(gf, out_t);

    ggml_backend_buffer_t in_buf = ggml_backend_alloc_ctx_tensors(ctx_in, m.backend);
    if (!in_buf) {
        fprintf(stderr, "[fatal] aux input buffer alloc failed\n");
        ggml_free(ctx); ggml_free(ctx_in); return false;
    }
    ggml_gallocr_t ga = ggml_gallocr_new(ggml_backend_get_default_buffer_type(m.backend));
    if (!ggml_gallocr_alloc_graph(ga, gf)) {
        fprintf(stderr, "[fatal] aux graph alloc failed\n");
        ggml_backend_buffer_free(in_buf);
        ggml_gallocr_free(ga); ggml_free(ctx); ggml_free(ctx_in); return false;
    }

    ggml_backend_tensor_set(c_in, cond_host.data(), 0, cond_host.size() * 4);
    ggml_status st = ggml_backend_graph_compute(m.backend, gf);
    if (st != GGML_STATUS_SUCCESS) {
        fprintf(stderr, "[fatal] aux compute = %d\n", (int)st);
        ggml_backend_buffer_free(in_buf);
        ggml_gallocr_free(ga); ggml_free(ctx); ggml_free(ctx_in); return false;
    }

    // out_t has ne=[M, T] (memory: T rows of M floats == [T, M] row-major).
    out_mel.resize((size_t)out_t->ne[0] * out_t->ne[1]);
    ggml_backend_tensor_get(out_t, out_mel.data(), 0, out_mel.size() * 4);

    // The aux_decoder is trained to emit *normalized* mel (after AuxDecoderAdaptor.norm_spec
    // halves the range to [-1, 1]). When called with infer=True the adaptor applies
    // denorm_spec immediately. Mirror that here so callers see the same units as
    // host-supplied `aux_mel`.
    if (out_t->ne[0] == M) {
        for (int t = 0; t < T; ++t) {
            for (int mi = 0; mi < M; ++mi) {
                size_t k = (size_t)t * M + mi;
                float mn = m.spec_min[mi], mx = m.spec_max[mi];
                float scale = (mx - mn) * 0.5f;
                float bias  = (mx + mn) * 0.5f;
                out_mel[k] = out_mel[k] * scale + bias;
            }
        }
    }

    ggml_backend_buffer_free(in_buf);
    ggml_gallocr_free(ga);
    ggml_free(ctx);
    ggml_free(ctx_in);
    return true;
}

static std::vector<float> pull_f32(const Model & m, const std::string & name, bool optional = false) {
    ggml_tensor * t = m.get(name, optional);
    if (!t) return {};
    std::vector<float> out(ggml_nelements(t));
    ggml_backend_tensor_get(t, out.data(), 0, ggml_nbytes(t));
    return out;
}

static float gelu_erf_scalar(float x) {
    return 0.5f * x * (1.0f + std::erf(x * 0.7071067811865475f));
}

static void add_linear_1d(std::vector<float> & cond,
                          int T,
                          int H,
                          const std::vector<float> & input,
                          float scale,
                          const std::vector<float> & w,
                          const std::vector<float> & b) {
    if (input.empty()) return;
    for (int t = 0; t < T; ++t) {
        const float x = input[(size_t)t] * scale;
        float * row = cond.data() + (size_t)t * H;
        for (int j = 0; j < H; ++j) row[j] += w[(size_t)j] * x + b[(size_t)j];
    }
}

static void matvec(const std::vector<float> & w,
                   const std::vector<float> & b,
                   const float * x,
                   int in_dim,
                   float * y,
                   int out_dim) {
    for (int o = 0; o < out_dim; ++o) {
        const float * wr = w.data() + (size_t)o * in_dim;
        float sum = b.empty() ? 0.0f : b[(size_t)o];
        for (int i = 0; i < in_dim; ++i) sum += wr[i] * x[i];
        y[o] = sum;
    }
}

static void apply_stretch_embed_host(const Model & m,
                                     const std::vector<int32_t> & mel2ph,
                                     const std::vector<int32_t> & durations,
                                     int T,
                                     int H,
                                     std::vector<float> & cond) {
    if (!m.cfg.use_stretch_embed) return;
    const std::vector<float> w1 = pull_f32(m, "fs2.stretch_embed.1.weight", true);
    if (w1.empty()) return;
    const std::vector<float> b1 = pull_f32(m, "fs2.stretch_embed.1.bias");
    const std::vector<float> w3 = pull_f32(m, "fs2.stretch_embed.3.weight");
    const std::vector<float> b3 = pull_f32(m, "fs2.stretch_embed.3.bias");
    const std::vector<float> wih = pull_f32(m, "fs2.stretch_rnn.weight_ih");
    const std::vector<float> whh = pull_f32(m, "fs2.stretch_rnn.weight_hh");
    const std::vector<float> bih = pull_f32(m, "fs2.stretch_rnn.bias_ih");
    const std::vector<float> bhh = pull_f32(m, "fs2.stretch_rnn.bias_hh");

    const int H4 = H * 4;
    const int H3 = H * 3;
    std::vector<float> stretch_emb((size_t)T * H);
    std::vector<float> sin_in((size_t)H);
    std::vector<float> hidden4((size_t)H4);
    const int half = H / 2;
    const float denom = half > 1 ? (float)(half - 1) : 1.0f;
    const float freq_scale = std::log(10000.0f) / denom;
    float stretch_cumsum = 0.0f;
    for (int t = 0; t < T; ++t) {
        int ph = mel2ph[(size_t)t];
        int dur = ph > 0 && ph <= (int)durations.size() ? std::max(1, durations[(size_t)ph - 1]) : 1;
        if (t == 0) {
            stretch_cumsum = 0.0f;
        } else {
            const bool boundary = mel2ph[(size_t)t] > mel2ph[(size_t)t - 1];
            const int prev_ph = mel2ph[(size_t)t - 1];
            const int prev_dur = prev_ph > 0 && prev_ph <= (int)durations.size()
                ? std::max(1, durations[(size_t)prev_ph - 1])
                : 1;
            stretch_cumsum += boundary ? (1.0f - (float)prev_dur) : 1.0f;
        }
        int stretch_idx = (int)std::lround(1000.0f * (stretch_cumsum / (float)dur));
        stretch_idx = std::max(0, std::min(1000, stretch_idx));
        for (int i = 0; i < half; ++i) {
            const float v = (float)stretch_idx * std::exp(-freq_scale * i);
            sin_in[(size_t)i] = std::sin(v);
            sin_in[(size_t)half + i] = std::cos(v);
        }
        matvec(w1, b1, sin_in.data(), H, hidden4.data(), H4);
        for (float & v : hidden4) v = gelu_erf_scalar(v);
        matvec(w3, b3, hidden4.data(), H4, stretch_emb.data() + (size_t)t * H, H);
    }

    for (size_t i = 0; i < cond.size(); ++i) cond[i] += stretch_emb[i];

    std::vector<float> h_prev((size_t)H, 0.0f);
    std::vector<float> h_next((size_t)H);
    std::vector<float> gates_i((size_t)H3);
    std::vector<float> gates_h((size_t)H3);
    std::vector<float> r((size_t)H), z((size_t)H), n((size_t)H);
    std::vector<float> rnn_out((size_t)T * H);
    for (int t = 0; t < T; ++t) {
        const float * x = cond.data() + (size_t)t * H;
        matvec(wih, bih, x, H, gates_i.data(), H3);
        matvec(whh, bhh, h_prev.data(), H, gates_h.data(), H3);
        for (int j = 0; j < H; ++j) {
            r[(size_t)j] = 1.0f / (1.0f + std::exp(-(gates_i[(size_t)j] + gates_h[(size_t)j])));
            z[(size_t)j] = 1.0f / (1.0f + std::exp(-(gates_i[(size_t)H + j] + gates_h[(size_t)H + j])));
            n[(size_t)j] = std::tanh(gates_i[(size_t)2 * H + j] + r[(size_t)j] * gates_h[(size_t)2 * H + j]);
            h_next[(size_t)j] = (1.0f - z[(size_t)j]) * n[(size_t)j] + z[(size_t)j] * h_prev[(size_t)j];
            rnn_out[(size_t)t * H + j] = h_next[(size_t)j];
        }
        h_prev.swap(h_next);
    }
    for (size_t i = 0; i < cond.size(); ++i) cond[i] += rnn_out[i];
}

static void apply_frame_embeddings_host(const Model & m,
                                        const InferenceInputs & in,
                                        const std::vector<int32_t> & mel2ph,
                                        const std::vector<float> & f0_log,
                                        int T,
                                        int H,
                                        std::vector<float> & cond) {
    apply_stretch_embed_host(m, mel2ph, in.durations, T, H, cond);

    if (m.cfg.use_spk_id && m.has_spk_embed) {
        const std::vector<float> spk = pull_f32(m, "fs2.spk_embed.weight");
        int sid = std::max(0, std::min(in.spk_id, (int)m.cfg.num_spk - 1));
        const float * row = spk.data() + (size_t)sid * H;
        for (int t = 0; t < T; ++t) {
            float * dst = cond.data() + (size_t)t * H;
            for (int j = 0; j < H; ++j) dst[j] += row[j];
        }
    }

    add_linear_1d(cond, T, H, f0_log, 1.0f,
                  pull_f32(m, "fs2.pitch_embed.weight"),
                  pull_f32(m, "fs2.pitch_embed.bias"));

    struct HostEmb {
        const char * prefix;
        const std::vector<float> * data;
        float scale;
    };
    const float var_scale = m.cfg.use_variance_scaling ? (1.0f / 96.0f) : 1.0f;
    const HostEmb embeds[] = {
        {"fs2.breathiness",     &in.breathiness, var_scale},
        {"fs2.voicing",         &in.voicing,     var_scale},
        {"fs2.tension",         &in.tension,     m.cfg.use_variance_scaling ? 0.1f : 1.0f},
        {"fs2.energy",          &in.energy,      var_scale},
        {"fs2.key_shift_embed", &in.gender,      m.cfg.use_variance_scaling ? (1.0f / 12.0f) : 1.0f},
        {"fs2.speed_embed",     &in.velocity,    1.0f},
    };
    for (const HostEmb & e : embeds) {
        if (e.data->empty()) continue;
        std::string p(e.prefix);
        std::vector<float> w = pull_f32(m, p + ".weight", true);
        if (w.empty()) continue;
        std::vector<float> b = pull_f32(m, p + ".bias", true);
        add_linear_1d(cond, T, H, *e.data, e.scale, w, b);
    }
}

bool run_inference(const Model & m, const InferenceInputs & in, InferenceOutputs & out) {
    const Config & c = m.cfg;
    const int L  = (int)in.tokens.size();
    const int H  = (int)c.hidden_size;
    const int NH = (int)c.num_heads;
    const int HD = H / NH;
    const int M  = (int)c.mel_bins;
    const int NL = (int)c.enc_layers;
    const int FK = (int)c.enc_ffn_kernel_size;
    const float sq_H = std::sqrt((float)H);

    int T = 0;
    std::vector<int32_t> durations = in.durations;
    std::vector<int32_t> mel2ph;
    if (!in.mel2ph.empty()) {
        mel2ph = in.mel2ph;
        T = (int)mel2ph.size();
        durations.assign((size_t)L, 0);
        for (int idx : mel2ph) {
            if (idx < 0 || idx > L) {
                fprintf(stderr, "[err] mel2ph index out of range: %d for L=%d\n", idx, L);
                return false;
            }
            if (idx > 0) durations[(size_t)idx - 1]++;
        }
    } else {
        if ((int)durations.size() != L) { fprintf(stderr, "[err] len mismatch\n"); return false; }
        mel2ph = build_mel2ph(durations, T);
    }
    if ((int)in.f0.size() != T) { fprintf(stderr, "[err] f0 len\n"); return false; }

    // f0 → log-mel pitch
    std::vector<float> f0_log(T);
    for (int i = 0; i < T; ++i) f0_log[i] = std::log1p(std::max(0.f, in.f0[i]) / 700.f);
    // durations as float
    std::vector<float> dur_f(L);
    for (int i = 0; i < L; ++i) {
        dur_f[i] = c.use_variance_scaling
            ? std::log1p((float)std::max(0, durations[i]))
            : (float)durations[i];
    }
    std::vector<int32_t> spk_index(1, std::max(0, std::min(in.spk_id, (int)c.num_spk - 1)));

    // ---------- build graph ------------------------------------------------
    ggml_init_params gp { /*mem*/ 256ULL * 1024 * 1024, nullptr, /*no_alloc*/ true };
    ggml_context * ctx = ggml_init(gp);
    ggml_cgraph * gf = ggml_new_graph_custom(ctx, GGML_DEFAULT_GRAPH_SIZE * 16, false);

    // inputs
    ggml_tensor * tok_t  = ggml_new_tensor_1d(ctx, GGML_TYPE_I32, L);
    ggml_set_input(tok_t);  ggml_set_name(tok_t, "tok");
    ggml_tensor * m2p_t  = ggml_new_tensor_1d(ctx, GGML_TYPE_I32, T);
    ggml_set_input(m2p_t);  ggml_set_name(m2p_t, "m2p");
    ggml_tensor * dur_t  = ggml_new_tensor_2d(ctx, GGML_TYPE_F32, 1, L);
    ggml_set_input(dur_t);  ggml_set_name(dur_t, "dur");
    ggml_tensor * npmask  = ggml_new_tensor_2d(ctx, GGML_TYPE_F32, 1, L);
    ggml_set_input(npmask);  ggml_set_name(npmask, "npmask");
    ggml_tensor * amask   = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, L);
    ggml_set_input(amask);   ggml_set_name(amask, "amask");
    ggml_tensor * pos_t = nullptr;
    if (c.use_rope) {
        pos_t = ggml_new_tensor_1d(ctx, GGML_TYPE_I32, L);
        ggml_set_input(pos_t); ggml_set_name(pos_t, "pos");
    }
    ggml_tensor * spk_t = nullptr;
    ggml_tensor * spk_embed = nullptr;
    if (c.use_spk_id && m.has_spk_embed) {
        spk_t = ggml_new_tensor_1d(ctx, GGML_TYPE_I32, 1);
        ggml_set_input(spk_t); ggml_set_name(spk_t, "spk");
        spk_embed = ggml_get_rows(ctx, m.get("fs2.spk_embed.weight"), spk_t);
    }

    // ---- 1  token embed → ne {H, L} ----
    ggml_tensor * x = ggml_get_rows(ctx, m.get("fs2.txt_embed.weight"), tok_t);
    x = ggml_scale(ctx, x, sq_H);

    // ---- 2  dur embed (+ lang embed) → extra {H, L} ----
    ggml_tensor * extra = ggml_add(ctx,
        ggml_mul_mat(ctx, m.get("fs2.dur_embed.weight"), dur_t),
        m.get("fs2.dur_embed.bias"));

    if (m.has_lang_embed && !in.languages.empty()) {
        ggml_tensor * lang_t = ggml_new_tensor_1d(ctx, GGML_TYPE_I32, L);
        ggml_set_input(lang_t); ggml_set_name(lang_t, "lang");
        extra = ggml_add(ctx, extra, ggml_get_rows(ctx, m.get("fs2.lang_embed.weight"), lang_t));
    }
    x = ggml_add(ctx, x, extra);

    // ---- 3  positional encoding (non-RoPE path: RelPosEnc) ----
    if (!c.use_rope) {
        ggml_tensor * pe_full = m.get("fs2.enc.pe", /*optional*/ true);
        if (pe_full) {
            x = ggml_scale(ctx, x, sq_H);                        // *= sqrt(H)
            ggml_tensor * pe = ggml_view_2d(ctx, pe_full, H, L, pe_full->nb[1], 0);
            x = ggml_add(ctx, x, pe);
        }
    }

    x = ggml_mul(ctx, x, npmask);                                // zero-out padding

    // ---- 4  transformer layers ----
    for (int li = 0; li < NL; ++li) {
        ggml_tensor * res = x;

        // ---- self-attention ----
        ggml_tensor * h = acoustic_layer_norm(ctx, m, c, x, spk_embed, li, 1, H);

        // QKV  {3H, L}
        ggml_tensor * qkv = ggml_mul_mat(ctx, m.get(tn("fs2.enc.%d.attn.in_proj.weight", li)), h);
        // split — views are non-contiguous because stride = 3H, so ggml_cont each
        ggml_tensor * Q = ggml_cont(ctx, ggml_view_2d(ctx, qkv, H, L, qkv->nb[1], 0));
        ggml_tensor * K = ggml_cont(ctx, ggml_view_2d(ctx, qkv, H, L, qkv->nb[1], H*sizeof(float)));
        ggml_tensor * V = ggml_cont(ctx, ggml_view_2d(ctx, qkv, H, L, qkv->nb[1], 2*H*sizeof(float)));

        // reshape {HD, NH, L}; apply RoPE here (positions indexed by ne[2]=L);
        // then permute Q,K to {HD, L, NH}; V to {L, HD, NH}.
        ggml_tensor * Q3 = ggml_reshape_3d(ctx, Q, HD, NH, L);
        ggml_tensor * K3 = ggml_reshape_3d(ctx, K, HD, NH, L);
        ggml_tensor * V3 = ggml_reshape_3d(ctx, V, HD, NH, L);
        if (c.use_rope) {
            const int rope_mode = c.rope_interleaved ? 0 /*GGML_ROPE_TYPE_NORMAL*/ : 2 /*GGML_ROPE_TYPE_NEOX*/;
            Q3 = ggml_rope_ext(ctx, ggml_cont(ctx, Q3), pos_t, nullptr,
                               HD, rope_mode, /*n_ctx_orig=*/0,
                               /*freq_base=*/10000.0f, /*freq_scale=*/1.0f,
                               0.0f, 1.0f, 32.0f, 1.0f);
            K3 = ggml_rope_ext(ctx, ggml_cont(ctx, K3), pos_t, nullptr,
                               HD, rope_mode, 0, 10000.0f, 1.0f, 0.0f, 1.0f, 32.0f, 1.0f);
        }
        Q = ggml_cont(ctx, ggml_permute(ctx, Q3, 0, 2, 1, 3));
        K = ggml_cont(ctx, ggml_permute(ctx, K3, 0, 2, 1, 3));
        V = ggml_cont(ctx, ggml_permute(ctx, V3, 1, 2, 0, 3));

        // scores {L, L, NH}
        ggml_tensor * sc = ggml_scale(ctx,
            ggml_mul_mat(ctx, K, Q),
            1.f / std::sqrt((float)HD));
        // mask: amask {L} reshaped to {L,1,1} — broadcasts over query & head
        sc = ggml_add(ctx, sc, ggml_reshape_3d(ctx, amask, L, 1, 1));
        sc = ggml_soft_max(ctx, sc);

        // attn out {HD, L, NH}
        ggml_tensor * ao = ggml_mul_mat(ctx, V, sc);
        ao = ggml_cont(ctx, ggml_permute(ctx, ao, 0, 2, 1, 3));  // {HD, NH, L}
        ao = ggml_reshape_2d(ctx, ao, H, L);

        // out proj
        h = ggml_mul_mat(ctx, m.get(tn("fs2.enc.%d.attn.out_proj.weight", li)), ao);
        x = ggml_mul(ctx, ggml_add(ctx, res, h), npmask);

        // ---- FFN ----
        res = x;
        h = acoustic_layer_norm(ctx, m, c, x, spk_embed, li, 2, H);

        // Conv1d(H, 4H, k, pad=k/2)
        ggml_tensor * h_t = ggml_cont(ctx, ggml_transpose(ctx, h));           // {L, H}
        ggml_tensor * ffn1w = m.get(tn("fs2.enc.%d.ffn1.weight", li));        // ne={K, IC, OC}
        ggml_tensor * ffn1w16 = ggml_cast(ctx, ffn1w, GGML_TYPE_F16);
        ggml_tensor * ff = ggml_conv_1d(ctx, ffn1w16, h_t, 1, FK/2, 1);       // {L_out, OC}
        ff = ggml_cont(ctx, ggml_transpose(ctx, ff));                          // {OC, L_out}
        ff = ggml_add(ctx, ff, m.get(tn("fs2.enc.%d.ffn1.bias", li)));
        ff = ggml_scale(ctx, ff, 1.f / std::sqrt((float)FK));

        if (c.ffn_act == "gelu")      ff = ggml_gelu_erf(ctx, ff);
        else if (c.ffn_act == "relu")  ff = ggml_relu(ctx, ff);
        else if (c.ffn_act == "swish") ff = ggml_silu(ctx, ff);
        else if (c.ffn_act == "swiglu") {
            // split {8H, L} → out {4H,L}, gate {4H,L}; result = out * silu(gate)
            int f4H = (int)ff->ne[0] / 2;
            ggml_tensor * o = ggml_cont(ctx, ggml_view_2d(ctx, ff, f4H, L, ff->nb[1], 0));
            ggml_tensor * g = ggml_cont(ctx, ggml_view_2d(ctx, ff, f4H, L, ff->nb[1], f4H*sizeof(float)));
            ff = ggml_mul(ctx, o, ggml_silu(ctx, g));
        }
        else ff = ggml_gelu_erf(ctx, ff);

        // Linear(4H, H)
        ff = ggml_add(ctx,
            ggml_mul_mat(ctx, m.get(tn("fs2.enc.%d.ffn2.weight", li)), ff),
            m.get(tn("fs2.enc.%d.ffn2.bias", li)));

        x = ggml_mul(ctx, ggml_add(ctx, res, ff), npmask);
    }

    // ---- 5  final LN ----
    x = ggml_norm(ctx, x, 1e-12f);
    x = ggml_add(ctx, ggml_mul(ctx, x, m.get("fs2.enc.final_ln.weight")),
                 m.get("fs2.enc.final_ln.bias"));
    x = ggml_mul(ctx, x, npmask);

    // ---- 6  length regulation: pad row-0 + gather ----
    ggml_tensor * zp = ggml_new_tensor_2d(ctx, GGML_TYPE_F32, H, 1);
    ggml_set_input(zp); ggml_set_name(zp, "zp");
    ggml_tensor * cond = ggml_get_rows(ctx, ggml_concat(ctx, zp, x, 1), m2p_t);

    // ---- output selection ----
    ggml_set_output(cond); ggml_set_name(cond, "cond_base");
    ggml_build_forward_expand(gf, cond);

    // ---------- alloc + fill encoder graph -------------------------------
    ggml_gallocr_t ga = ggml_gallocr_new(ggml_backend_get_default_buffer_type(m.backend));
    if (!ggml_gallocr_alloc_graph(ga, gf)) {
        fprintf(stderr, "[fatal] encoder alloc failed\n");
        ggml_gallocr_free(ga); ggml_free(ctx); return false;
    }

    ggml_backend_tensor_set(tok_t, in.tokens.data(),  0, L*4);
    ggml_backend_tensor_set(m2p_t, mel2ph.data(),     0, T*4);
    ggml_backend_tensor_set(dur_t, dur_f.data(),      0, L*4);
    if (spk_t && spk_t->buffer) ggml_backend_tensor_set(spk_t, spk_index.data(), 0, sizeof(int32_t));

    std::vector<float> np_v(L), am_v(L);
    for (int i = 0; i < L; ++i) {
        np_v[i] = (in.tokens[i] != 0) ? 1.f : 0.f;
        am_v[i] = (in.tokens[i] != 0) ? 0.f : -INFINITY;
    }
    ggml_backend_tensor_set(npmask, np_v.data(), 0, L*4);
    ggml_backend_tensor_set(amask,  am_v.data(), 0, L*4);
    if (pos_t && pos_t->buffer) {
        std::vector<int32_t> positions(L);
        for (int i = 0; i < L; ++i) positions[i] = i;
        ggml_backend_tensor_set(pos_t, positions.data(), 0, L*4);
    }

    { std::vector<float> z(H, 0.f);
      ggml_backend_tensor_set(ggml_get_tensor(ctx, "zp"), z.data(), 0, H*4); }

    if (m.has_lang_embed && !in.languages.empty()) {
        ggml_tensor * lt = ggml_get_tensor(ctx, "lang");
        if (lt) ggml_backend_tensor_set(lt, in.languages.data(), 0, L*4);
    }
    // ---------- compute encoder ------------------------------------------
    ggml_status st = ggml_backend_graph_compute(m.backend, gf);
    if (st != GGML_STATUS_SUCCESS) {
        fprintf(stderr, "[fatal] encoder compute = %d\n", (int)st);
        ggml_gallocr_free(ga); ggml_free(ctx); return false;
    }

    // Pull cond [H, T] (memory layout = T rows of H elements, since ne[0]=H, ne[1]=T)
    std::vector<float> cond_host((size_t)H * T);
    ggml_backend_tensor_get(cond, cond_host.data(), 0, cond_host.size() * 4);
    InferenceInputs frame_in = in;
    frame_in.durations = durations;
    apply_frame_embeddings_host(m, frame_in, mel2ph, f0_log, T, H, cond_host);

    ggml_gallocr_free(ga);
    ggml_free(ctx);

    // For Cond mode: dump the final acoustic condition after stretch, speaker,
    // pitch, variance, key-shift, and speed embeddings.
    if (in.mode == OutputMode::Cond) {
        out.T = T; out.dim = H;
        out.data = std::move(cond_host);
        return true;
    }

    // ====================================================================
    // ============ Phase 4-5: LYNXNet velocity + Euler sampler ===========
    // ====================================================================
    if (c.backbone_type != "lynxnet2") {
        fprintf(stderr, "[fatal] backbone '%s' not implemented\n", c.backbone_type.c_str());
        return false;
    }
    if (c.diffusion_type != "reflow") {
        fprintf(stderr, "[fatal] diffusion '%s' not implemented\n", c.diffusion_type.c_str());
        return false;
    }
    if (c.bb_glu_type != "atanglu" || !c.bb_use_conditioner_cache) {
        fprintf(stderr, "[fatal] unsupported lynxnet2 config: glu=%s conditioner_cache=%d\n",
                c.bb_glu_type.c_str(), (int)c.bb_use_conditioner_cache);
        return false;
    }

    const int   C  = (int)c.bb_num_channels;
    const int   BL = (int)c.bb_num_layers;
    const int   BK = (int)c.bb_kernel_size;
    const float ts_factor = (float)c.time_scale_factor;
    const int   steps = (in.sampling_steps_override > 0)
                            ? in.sampling_steps_override
                            : (int)c.sampling_steps;

    // ---- build backbone single-step graph (reused per Euler step) ----
    // Inputs live in a SEPARATE context + buffer so the gallocr (which only
    // owns intermediates and outputs) cannot alias their storage with scratch
    // tensors. Without this split, a long compute graph reuses the input
    // buffer between iterations and silently corrupts the next step's input.
    ggml_init_params bp_in { /*mem*/ ggml_tensor_overhead() * 32, nullptr, /*no_alloc*/ true };
    ggml_context * bctx_in = ggml_init(bp_in);
    ggml_init_params bp { /*mem*/ 1024ULL * 1024 * 1024, nullptr, /*no_alloc*/ true };
    ggml_context * bctx = ggml_init(bp);
    ggml_cgraph * bgf = ggml_new_graph_custom(bctx, GGML_DEFAULT_GRAPH_SIZE * 64, false);

    // inputs: x_t [M, T], cond_in [H, T]
    ggml_tensor * x_in   = ggml_new_tensor_2d(bctx_in, GGML_TYPE_F32, M, T);
    ggml_set_input(x_in);  ggml_set_name(x_in, "x_in");
    ggml_tensor * c_in   = ggml_new_tensor_2d(bctx_in, GGML_TYPE_F32, H, T);
    ggml_set_input(c_in);  ggml_set_name(c_in, "c_in");

    // ----------- diffusion step embedding -----------
    int half = C / 2;
    std::vector<float> freq_host(half);
    {
        float scale = std::log(10000.f) / std::max(1, half - 1);
        for (int i = 0; i < half; ++i) freq_host[i] = std::exp(-scale * i);
    }
    ggml_tensor * tf_in = ggml_new_tensor_1d(bctx_in, GGML_TYPE_F32, half);
    ggml_set_input(tf_in); ggml_set_name(tf_in, "tf_in");
    ggml_tensor * tf = tf_in;
    ggml_tensor * s  = ggml_sin(bctx, tf);
    ggml_tensor * co = ggml_cos(bctx, tf);
    ggml_tensor * de = ggml_concat(bctx, s, co, 0);
    de = ggml_reshape_2d(bctx, de, C, 1);
    de = linear(bctx, m, "bb.diff.1", de);
    de = ggml_gelu_erf(bctx, de);
    de = linear(bctx, m, "bb.diff.3", de);

    ggml_tensor * x_bb = linear(bctx, m, "bb.in", x_in);
    x_bb = ggml_add(bctx, x_bb, conv1d_k1_as_linear(bctx, m, "bb.cond", c_in));
    x_bb = ggml_add(bctx, x_bb, de);

    for (int li = 0; li < BL; ++li) {
        ggml_tensor * res = x_bb;
        ggml_tensor * h = ggml_norm(bctx, x_bb, 1e-5f);
        h = ggml_add(bctx,
            ggml_mul(bctx, h, m.get(tn("bb.r.%d.net.0.weight", li))),
            m.get(tn("bb.r.%d.net.0.bias", li)));

        ggml_tensor * dw = ggml_cast(bctx, m.get(tn("bb.r.%d.net.2.weight", li)), GGML_TYPE_F16);
        ggml_tensor * ht = ggml_cont(bctx, ggml_transpose(bctx, h));
        h = ggml_conv_1d_dw(bctx, dw, ht, 1, BK / 2, 1);
        h = ggml_cont(bctx, ggml_transpose(bctx, h));
        h = ggml_add(bctx, h, m.get(tn("bb.r.%d.net.2.bias", li)));

        h = atanglu(bctx, linear(bctx, m, tn("bb.r.%d.net.4", li), h));
        h = atanglu(bctx, linear(bctx, m, tn("bb.r.%d.net.6", li), h));
        h = linear(bctx, m, tn("bb.r.%d.net.8", li), h);
        x_bb = ggml_add(bctx, res, h);
    }
    x_bb = ggml_norm(bctx, x_bb, 1e-5f);
    x_bb = ggml_add(bctx,
        ggml_mul(bctx, x_bb, m.get("bb.norm.weight")),
        m.get("bb.norm.bias"));
    ggml_tensor * v = linear(bctx, m, "bb.out", x_bb);

    ggml_set_output(v); ggml_set_name(v, "v_out");
    ggml_build_forward_expand(bgf, v);

    // Allocate input buffer (persistent across compute calls).
    ggml_backend_buffer_t in_buf = ggml_backend_alloc_ctx_tensors(bctx_in, m.backend);
    if (!in_buf) {
        fprintf(stderr, "[fatal] backbone input buffer alloc failed\n");
        ggml_free(bctx); ggml_free(bctx_in); return false;
    }

    ggml_gallocr_t bga = ggml_gallocr_new(ggml_backend_get_default_buffer_type(m.backend));
    if (!ggml_gallocr_alloc_graph(bga, bgf)) {
        fprintf(stderr, "[fatal] backbone alloc failed\n");
        ggml_gallocr_free(bga);
        ggml_backend_buffer_free(in_buf);
        ggml_free(bctx); ggml_free(bctx_in); return false;
    }

    auto safe_set = [](ggml_tensor * t, const void * data, size_t bytes) {
        if (t && t->buffer) ggml_backend_tensor_set(t, data, 0, bytes);
    };
    safe_set(c_in,   cond_host.data(), cond_host.size() * 4);

    // ---- init x_t from seeded Gaussian noise ----
    std::mt19937 rng(in.seed);
    std::normal_distribution<float> nd(0.f, 1.f);
    std::vector<float> x_t((size_t)M * T);
    for (auto & v_ : x_t) v_ = nd(rng);

    // DEBUG override: load fixed noise and condition from raw float32 files.
    if (const char * np_path = std::getenv("DSDIAG_NOISE")) {
        FILE * f = std::fopen(np_path, "rb");
        if (f) {
            size_t r = std::fread(x_t.data(), sizeof(float), x_t.size(), f);
            (void)r;
            std::fclose(f);
            fprintf(stderr, "[debug] loaded noise from %s (%zu floats)\n", np_path, x_t.size());
        }
    }
    if (const char * cd_path = std::getenv("DSDIAG_COND")) {
        FILE * f = std::fopen(cd_path, "rb");
        if (f) {
            size_t r = std::fread(cond_host.data(), sizeof(float), cond_host.size(), f);
            (void)r;
            std::fclose(f);
            fprintf(stderr, "[debug] loaded cond from %s (%zu floats)\n", cd_path, cond_host.size());
            safe_set(c_in, cond_host.data(), cond_host.size() * 4);
        }
    }
    float t_start = c.t_start_infer;
    if (c.use_variable_depth) {
        // Match DiffSinger shallow diffusion semantics:
        // t_start = max(1 - depth, 1 - max_depth).
        float ts_d  = 1.f - in.depth;
        float ts_md = 1.f - c.max_depth;
        t_start = std::max(ts_d, ts_md);
        // Also need to spec-normalize aux_mel when t_start > 0
    }
    if (t_start < 0.f) t_start = 0.f;
    if (t_start > 1.f) t_start = 1.f;
    int n = std::max(1, steps);
    float dt = (1.f - t_start) / (float)n;

    // ---- init x_t: t_start * x_end + (1-t_start) * noise ----
    std::vector<float> aux_mel_buf;        // backing storage when we run aux_decoder ourselves
    const std::vector<float> * aux_src = &in.aux_mel;
    if (t_start > 0.f) {
        if (in.aux_mel.empty() && c.has_aux_decoder) {
            // No host-supplied aux mel — generate one with the ggml-ported
            // ConvNeXt aux decoder.
            if (!run_aux_decoder(m, cond_host, T, H, M, aux_mel_buf)) {
                fprintf(stderr,
                    "[fatal] aux_decoder forward failed; pass --aux-mel\n");
                return false;
            }
            aux_src = &aux_mel_buf;
            fprintf(stderr, "[info] generated aux_mel via ggml aux_decoder (T=%d)\n", T);
            if (const char * dump = std::getenv("DSDIAG_AUX_OUT")) {
                FILE * f = std::fopen(dump, "wb");
                if (f) {
                    std::fwrite(aux_mel_buf.data(), 4, aux_mel_buf.size(), f);
                    std::fclose(f);
                    fprintf(stderr, "[debug] wrote aux mel to %s\n", dump);
                }
            }
        }
        if (aux_src->size() != (size_t)T * M) {
            fprintf(stderr,
                "[fatal] use_variable_depth=true with depth=%.2f requires aux_mel of size T*M=%d "
                "(got %zu, model.has_aux_decoder=%d)\n",
                in.depth, T * M, aux_src->size(), (int)c.has_aux_decoder);
            return false;
        }
        // x_end = norm_spec(aux_mel) = (aux - smin) / (smax - smin) * 2 - 1
        // mix: x = t_start * x_end + (1 - t_start) * noise
        for (int t = 0; t < T; ++t) {
            for (int mi = 0; mi < M; ++mi) {
                float a = (*aux_src)[(size_t)t * M + mi];
                float mn = m.spec_min[mi], mx = m.spec_max[mi];
                float xe = (a - mn) / (mx - mn) * 2.f - 1.f;
                size_t k = (size_t)t * M + mi;
                x_t[k] = t_start * xe + (1.f - t_start) * x_t[k];
            }
        }
    }

    std::vector<float> v_host((size_t)M * T);
    std::vector<float> tf_host(half);

    // Helper: evaluate velocity at (x_eval, t_eval) and store into v_host.
    auto eval_velocity = [&](const std::vector<float> & x_eval, float t_eval) -> bool {
        safe_set(x_in, x_eval.data(), x_eval.size() * 4);
        float t_scaled = ts_factor * t_eval;
        for (int k = 0; k < half; ++k) tf_host[k] = freq_host[k] * t_scaled;
        safe_set(tf_in, tf_host.data(), tf_host.size() * sizeof(float));
        ggml_status bst = ggml_backend_graph_compute(m.backend, bgf);
        if (bst != GGML_STATUS_SUCCESS) return false;
        ggml_backend_tensor_get(v, v_host.data(), 0, v_host.size() * 4);
        return true;
    };

    const std::string alg = in.algorithm_override.empty() ? c.sampling_algorithm : in.algorithm_override;
    for (int i = 0; i < n; ++i) {
        float t_i = t_start + (float)i * dt;

        if (alg == "midpoint" || alg == "rk2") {
            // Midpoint method: k1 = v(x, t); x_mid = x + dt/2*k1;
            //                  k2 = v(x_mid, t+dt/2); x += dt*k2
            if (!eval_velocity(x_t, t_i)) goto compute_fail;
            std::vector<float> x_mid(x_t.size());
            for (size_t k = 0; k < x_t.size(); ++k)
                x_mid[k] = x_t[k] + 0.5f * dt * v_host[k];
            if (!eval_velocity(x_mid, t_i + 0.5f * dt)) goto compute_fail;
            for (size_t k = 0; k < x_t.size(); ++k)
                x_t[k] += dt * v_host[k];
        } else if (alg == "rk4") {
            // Classic 4th-order Runge-Kutta
            if (!eval_velocity(x_t, t_i)) goto compute_fail;
            std::vector<float> k1(v_host);
            std::vector<float> x_tmp(x_t.size());
            for (size_t k = 0; k < x_t.size(); ++k)
                x_tmp[k] = x_t[k] + 0.5f * dt * k1[k];
            if (!eval_velocity(x_tmp, t_i + 0.5f * dt)) goto compute_fail;
            std::vector<float> k2(v_host);
            for (size_t k = 0; k < x_t.size(); ++k)
                x_tmp[k] = x_t[k] + 0.5f * dt * k2[k];
            if (!eval_velocity(x_tmp, t_i + 0.5f * dt)) goto compute_fail;
            std::vector<float> k3(v_host);
            for (size_t k = 0; k < x_t.size(); ++k)
                x_tmp[k] = x_t[k] + dt * k3[k];
            if (!eval_velocity(x_tmp, t_i + dt)) goto compute_fail;
            // x += dt/6 * (k1 + 2*k2 + 2*k3 + k4)
            for (size_t k = 0; k < x_t.size(); ++k)
                x_t[k] += dt / 6.f * (k1[k] + 2.f*k2[k] + 2.f*k3[k] + v_host[k]);
        } else {
            // Euler (default)
            if (!eval_velocity(x_t, t_i)) goto compute_fail;
            for (size_t k = 0; k < x_t.size(); ++k)
                x_t[k] += dt * v_host[k];
        }

        if (const char * trj = std::getenv("DSDIAG_TRAJ_DIR")) {
            int targets[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 19, 20};
            for (int tg : targets) {
                if (i + 1 == tg) {
                    char path[256];
                    std::snprintf(path, sizeof path, "%s/x_step%d.bin", trj, tg);
                    FILE * f = std::fopen(path, "wb");
                    if (f) {
                        std::fwrite(x_t.data(), sizeof(float), x_t.size(), f);
                        std::fclose(f);
                    }
                }
            }
        }
    }
    goto sampler_done;
compute_fail:
    fprintf(stderr, "[fatal] backbone compute failed\n");
    ggml_gallocr_free(bga); ggml_backend_buffer_free(in_buf);
    ggml_free(bctx); ggml_free(bctx_in); return false;
sampler_done:

    ggml_gallocr_free(bga);
    ggml_backend_buffer_free(in_buf);
    ggml_free(bctx);
    ggml_free(bctx_in);

    // ---- denorm_spec on host: out = (x+1)/2 * (max-min) + min ----
    out.T = T; out.dim = M;
    out.data.resize((size_t)T * M);
    // x_t is stored row-major as T rows of M (ne[0]=M, ne[1]=T) — same as output layout
    for (int t = 0; t < T; ++t) {
        for (int mi = 0; mi < M; ++mi) {
            float xv = x_t[(size_t)t * M + mi];
            float mn = m.spec_min[mi];
            float mx = m.spec_max[mi];
            out.data[(size_t)t * M + mi] = (xv + 1.f) * 0.5f * (mx - mn) + mn;
        }
    }
    return true;
}

} // namespace ds
