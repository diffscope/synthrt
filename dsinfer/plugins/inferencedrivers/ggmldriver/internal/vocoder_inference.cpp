#include "vocoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace nsv {

static std::string tn(const char * fmt, int a) {
    char buf[128];
    snprintf(buf, sizeof buf, fmt, a);
    return buf;
}

static std::string tn2(const char * fmt, int a, int b) {
    char buf[128];
    snprintf(buf, sizeof buf, fmt, a, b);
    return buf;
}

static std::vector<float> make_fast_sine_source(const Config & c,
                                                const std::vector<float> & f0) {
    int tail = 1;
    for (size_t i = 2; i < c.upsample_rates.size(); ++i) {
        tail *= (int)c.upsample_rates[i];
    }
    const int upp = (int)c.hop_size / std::max(1, tail);
    const float source_sr = (float)c.sampling_rate / (float)std::max(1, tail);
    const int T = (int)f0.size();
    std::vector<float> out((size_t)T * upp);
    float acc = 0.0f;
    const float two_pi = 6.2831853071795864769f;
    for (int t = 0; t < T; ++t) {
        float s0 = f0[t] / source_sr;
        float ds0 = (t + 1 < T) ? ((f0[t + 1] - f0[t]) / source_sr) : 0.0f;
        float last = s0 * upp + 0.5f * ds0 * upp * (upp - 1) / (float)upp;
        float rad2 = std::fmod(last + 0.5f, 1.0f) - 0.5f;
        float prev_acc = acc;
        acc = std::fmod(acc + rad2, 1.0f);
        for (int n = 1; n <= upp; ++n) {
            float rad = s0 * n + 0.5f * ds0 * n * (n - 1) / (float)upp + prev_acc;
            out[(size_t)t * upp + (n - 1)] = std::sin(two_pi * rad);
        }
    }
    return out;
}

static ggml_tensor * bias_add_2d(ggml_context * ctx, ggml_tensor * x, ggml_tensor * bias) {
    ggml_tensor * b = ggml_reshape_2d(ctx, bias, 1, x->ne[1]);
    return ggml_add(ctx, x, b);
}

static ggml_tensor * conv1d_same(ggml_context * ctx,
                                 const Model & m,
                                 const std::string & prefix,
                                 ggml_tensor * x,
                                 int dilation = 1) {
    ggml_tensor * w = ggml_cast(ctx, m.get(prefix + ".weight"), GGML_TYPE_F16);
    int pad = (int)(w->ne[0] / 2) * dilation;
    ggml_tensor * y = ggml_conv_1d(ctx, w, x, 1, pad, dilation);
    y = ggml_reshape_2d(ctx, y, y->ne[0], y->ne[1]);
    y = bias_add_2d(ctx, y, m.get(prefix + ".bias"));
    return y;
}

static ggml_tensor * conv1d_k1(ggml_context * ctx,
                               const Model & m,
                               const std::string & prefix,
                               ggml_tensor * x) {
    ggml_tensor * w = m.get(prefix + ".weight");
    ggml_tensor * w2 = ggml_reshape_2d(ctx, w, w->ne[1], w->ne[2]);
    ggml_tensor * xt = ggml_cont(ctx, ggml_transpose(ctx, x)); // [C,T]
    ggml_tensor * y = ggml_mul_mat(ctx, w2, xt);              // [OC,T]
    y = ggml_cont(ctx, ggml_transpose(ctx, y));               // [T,OC]
    y = bias_add_2d(ctx, y, m.get(prefix + ".bias"));
    return y;
}

static ggml_tensor * conv_transpose1d_crop(ggml_context * ctx,
                                           const Model & m,
                                           const std::string & prefix,
                                           ggml_tensor * x,
                                           int stride,
                                           int kernel) {
    ggml_tensor * w = m.get(prefix + ".weight");
    ggml_tensor * y = ggml_conv_transpose_1d(ctx, w, x, stride, 0, 1);
    y = ggml_reshape_2d(ctx, y, y->ne[0], y->ne[1]);
    int pad = (kernel - stride) / 2;
    int64_t target_len = x->ne[0] * stride;
    if (pad > 0) {
        y = ggml_view_2d(ctx, y, target_len, y->ne[1], y->nb[1], pad * y->nb[0]);
        y = ggml_cont(ctx, y);
    }
    y = bias_add_2d(ctx, y, m.get(prefix + ".bias"));
    return y;
}

static ggml_tensor * resblock(ggml_context * ctx,
                              const Model & m,
                              ggml_tensor * x,
                              int rb_index) {
    ggml_tensor * h = x;
    for (int k = 0; k < 3; ++k) {
        ggml_tensor * xt = ggml_leaky_relu(ctx, h, 0.1f, false);
        xt = conv1d_same(ctx, m, tn2("resblocks.%d.convs1.%d", rb_index, k), xt,
                         k == 0 ? 1 : (k == 1 ? 3 : 5));
        xt = ggml_leaky_relu(ctx, xt, 0.1f, false);
        xt = conv1d_same(ctx, m, tn2("resblocks.%d.convs2.%d", rb_index, k), xt, 1);
        h = ggml_add(ctx, xt, h);
    }
    return h;
}

bool run_vocoder(const Model & m, const InferenceInputs & in, InferenceOutputs & out) {
    const Config & c = m.cfg;
    if (in.frames <= 0) {
        fprintf(stderr, "[err] frames must be positive\n");
        return false;
    }
    if (in.mel.size() != (size_t)in.frames * c.num_mels) {
        fprintf(stderr, "[err] mel size mismatch\n");
        return false;
    }
    if (in.f0.size() != (size_t)in.frames) {
        fprintf(stderr, "[err] f0 size mismatch\n");
        return false;
    }
    if (!c.mini_nsf) {
        fprintf(stderr, "[fatal] non-mini NSF source is not implemented\n");
        return false;
    }

    std::vector<float> mel_cf((size_t)c.num_mels * in.frames);
    const float ln10 = 2.30259f;
    for (int t = 0; t < in.frames; ++t) {
        for (uint32_t mi = 0; mi < c.num_mels; ++mi) {
            float v = in.mel[(size_t)t * c.num_mels + mi];
            if (in.mel_base10) v *= ln10;
            mel_cf[(size_t)mi * in.frames + t] = v; // ggml ne=[T,C], first dim is fastest
        }
    }
    std::vector<float> source = make_fast_sine_source(c, in.f0);
    std::vector<float> conv_pre_noise;
    const float noise_sigma = c.noise_sigma * in.noise_scale;
    if (noise_sigma > 0.0f) {
        std::mt19937 rng(in.seed);
        std::normal_distribution<float> norm(0.0f, noise_sigma);
        conv_pre_noise.resize((size_t)in.frames * c.upsample_initial_channel);
        for (float & v : conv_pre_noise) v = norm(rng);
    }

    const int64_t source_len = (int64_t)source.size();
    ggml_init_params ip_in { ggml_tensor_overhead() * 8, nullptr, true };
    ggml_context * ctx_in = ggml_init(ip_in);
    ggml_tensor * mel_in = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, in.frames, c.num_mels);
    ggml_set_input(mel_in);
    ggml_set_name(mel_in, "mel");
    ggml_tensor * src_in = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, source_len, 1);
    ggml_set_input(src_in);
    ggml_set_name(src_in, "source");
    ggml_tensor * noise_in = nullptr;
    if (!conv_pre_noise.empty()) {
        noise_in = ggml_new_tensor_2d(ctx_in, GGML_TYPE_F32, in.frames, c.upsample_initial_channel);
        ggml_set_input(noise_in);
        ggml_set_name(noise_in, "conv_pre_noise");
    }

    ggml_init_params ip { 1024ULL * 1024 * 1024, nullptr, true };
    ggml_context * ctx = ggml_init(ip);
    ggml_cgraph * gf = ggml_new_graph_custom(ctx, GGML_DEFAULT_GRAPH_SIZE * 64, false);

    ggml_tensor * x = conv1d_same(ctx, m, "conv_pre", mel_in, 1);
    if (noise_in) {
        x = ggml_add(ctx, x, noise_in);
    }
    const int num_kernels = (int)c.resblock_kernel_sizes.size();
    for (uint32_t i = 0; i < c.num_upsamples; ++i) {
        x = ggml_leaky_relu(ctx, x, 0.1f, false);
        x = conv_transpose1d_crop(ctx, m, tn("ups.%d", (int)i), x,
                                  (int)c.upsample_rates[i],
                                  (int)c.upsample_kernel_sizes[i]);
        if (i == 1) {
            ggml_tensor * xs = conv1d_k1(ctx, m, "source_conv", src_in);
            x = ggml_add(ctx, x, xs);
        }
        ggml_tensor * sum = nullptr;
        for (int j = 0; j < num_kernels; ++j) {
            ggml_tensor * rb = resblock(ctx, m, x, (int)i * num_kernels + j);
            sum = sum ? ggml_add(ctx, sum, rb) : rb;
        }
        x = ggml_scale(ctx, sum, 1.0f / (float)num_kernels);
    }
    x = ggml_leaky_relu(ctx, x, 0.1f, false);
    x = conv1d_same(ctx, m, "conv_post", x, 1);
    x = ggml_tanh(ctx, x);
    ggml_set_output(x);
    ggml_set_name(x, "wav");
    ggml_build_forward_expand(gf, x);

    ggml_backend_buffer_t in_buf = ggml_backend_alloc_ctx_tensors(ctx_in, m.backend);
    if (!in_buf) {
        fprintf(stderr, "[fatal] input buffer alloc failed\n");
        ggml_free(ctx);
        ggml_free(ctx_in);
        return false;
    }
    ggml_gallocr_t ga = ggml_gallocr_new(ggml_backend_get_default_buffer_type(m.backend));
    if (!ggml_gallocr_alloc_graph(ga, gf)) {
        fprintf(stderr, "[fatal] graph alloc failed\n");
        ggml_gallocr_free(ga);
        ggml_backend_buffer_free(in_buf);
        ggml_free(ctx);
        ggml_free(ctx_in);
        return false;
    }

    ggml_backend_tensor_set(mel_in, mel_cf.data(), 0, mel_cf.size() * sizeof(float));
    ggml_backend_tensor_set(src_in, source.data(), 0, source.size() * sizeof(float));
    if (noise_in) {
        ggml_backend_tensor_set(noise_in, conv_pre_noise.data(), 0, conv_pre_noise.size() * sizeof(float));
    }
    ggml_status st = ggml_backend_graph_compute(m.backend, gf);
    if (st != GGML_STATUS_SUCCESS) {
        fprintf(stderr, "[fatal] vocoder compute = %d\n", (int)st);
        ggml_gallocr_free(ga);
        ggml_backend_buffer_free(in_buf);
        ggml_free(ctx);
        ggml_free(ctx_in);
        return false;
    }

    out.samples = (int)x->ne[0];
    std::vector<float> tmp((size_t)x->ne[0] * x->ne[1]);
    ggml_backend_tensor_get(x, tmp.data(), 0, tmp.size() * sizeof(float));
    out.wav.resize(out.samples);
    for (int i = 0; i < out.samples; ++i) out.wav[i] = tmp[i];

    ggml_gallocr_free(ga);
    ggml_backend_buffer_free(in_buf);
    ggml_free(ctx);
    ggml_free(ctx_in);
    return true;
}

} // namespace nsv
