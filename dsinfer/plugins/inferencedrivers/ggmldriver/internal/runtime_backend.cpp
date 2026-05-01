#include "runtime_backend.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace dsrt {

static Precision g_precision = Precision::F32;

void set_precision(Precision p) { g_precision = p; }
Precision get_precision() { return g_precision; }

void set_backend_mode(const char * mode) {
#if defined(_WIN32)
    _putenv_s("DSGGML_BACKEND", mode ? mode : "");
#else
    setenv("DSGGML_BACKEND", mode ? mode : "", 1);
#endif
}

static int runtime_threads() {
    int nth = 4;
    if (const char * env = std::getenv("DSGGML_THREADS")) {
        int v = std::atoi(env);
        if (v > 0) nth = v;
    }
    return nth;
}

ggml_backend_t init_backend(const char * component) {
    // Per-component override env vars take precedence over the global one.
    // e.g. DSGGML_BACKEND_VOCODER=cpu while DSGGML_BACKEND=gpu lets us run
    // the heavy backbone+encoder on Metal but keep the vocoder on CPU
    // (vocoder is dominated by ggml_conv_transpose_1d which currently
    // compiles slowly on Metal and is already real-time on CPU).
    const char * requested = nullptr;
    if (component) {
        char buf[128];
        std::snprintf(buf, sizeof buf, "DSGGML_BACKEND_%s", component);
        // Uppercase the component name in-place.
        for (char * p = buf + 15; *p; ++p) {
            if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
        }
        requested = std::getenv(buf);
        if (requested && requested[0] == '\0') requested = nullptr;
    }
    if (!requested) requested = std::getenv("DSGGML_BACKEND");
    if (!requested || requested[0] == '\0') requested = "cpu";

    // On Apple platforms, the vocoder's ggml_conv_transpose_1d triggers very slow
    // Metal kernel JIT and can hang.  Force CPU unless the user explicitly set
    // DSGGML_BACKEND_VOCODER=gpu (per-component override already resolved above).
#if defined(__APPLE__)
    if (component && std::strcmp(component, "vocoder") == 0) {
        // Only override if the user did NOT explicitly set the per-component var.
        char buf[128];
        std::snprintf(buf, sizeof buf, "DSGGML_BACKEND_VOCODER");
        const char * explicit_voc = std::getenv(buf);
        if (!explicit_voc || explicit_voc[0] == '\0') {
            // Force CPU regardless of global --backend setting
            if (std::strcmp(requested, "gpu") == 0 || std::strcmp(requested, "auto") == 0) {
                fprintf(stderr, "[load] vocoder: forcing CPU (Metal conv_transpose_1d is unstable)\n");
                requested = "cpu";
            }
        }
    }
#endif

    ggml_backend_load_all();
    ggml_backend_t backend = nullptr;
    if (std::strcmp(requested, "gpu") == 0) {
        backend = ggml_backend_init_by_type(GGML_BACKEND_DEVICE_TYPE_GPU, nullptr);
        if (!backend) {
            backend = ggml_backend_init_by_type(GGML_BACKEND_DEVICE_TYPE_IGPU, nullptr);
        }
        if (!backend) {
            fprintf(stderr, "[fatal] %s requested GPU backend, but ggml did not initialize one\n", component);
            return nullptr;
        }
    } else if (std::strcmp(requested, "auto") == 0) {
        backend = ggml_backend_init_best();
    }
    if (!backend) {
        backend = ggml_backend_cpu_init();
    }
    if (!backend) {
        fprintf(stderr, "[fatal] %s backend init failed\n", component);
        return nullptr;
    }
    if (ggml_backend_is_cpu(backend)) {
        const int nth = runtime_threads();
        ggml_backend_cpu_set_n_threads(backend, nth);
        fprintf(stderr, "[load] %s backend: %s, threads=%d\n",
                component, ggml_backend_name(backend), nth);
    } else {
        fprintf(stderr, "[load] %s backend: %s\n", component, ggml_backend_name(backend));
    }
    return backend;
}

// Convert F32 data to F16 in-place (output buffer can be smaller)
static void convert_f32_to_f16(const float * src, void * dst, size_t n_elements) {
    ggml_fp16_t * out = (ggml_fp16_t *)dst;
    for (size_t i = 0; i < n_elements; ++i) {
        out[i] = ggml_fp32_to_fp16(src[i]);
    }
}

LoadResult load_gguf_weights(
    const std::string & path,
    gguf_context * gctx,
    ggml_context * ctx_meta,
    ggml_context * ctx_w,
    ggml_backend_t backend,
    std::unordered_map<std::string, ggml_tensor *> & tensors)
{
    LoadResult result;
    const bool do_f16 = (g_precision == Precision::F16);
    const int64_t n_tensors = gguf_get_n_tensors(gctx);

    // Phase 1: Create tensors (with optional F32→F16 type change)
    // Only convert 2D+ weight matrices to F16; keep 1D (biases, norms) as F32.
    // NOTE: Diffusion/reflow models (acoustic, variance, pitch) are extremely
    // precision-sensitive — F16 weights cause ODE accumulation errors that destroy
    // output quality. Only feed-forward models (vocoder) benefit from F16.
    // The caller should only enable F16 for appropriate models.
    for (int64_t i = 0; i < n_tensors; ++i) {
        const char * name = gguf_get_tensor_name(gctx, i);
        ggml_tensor * src = ggml_get_tensor(ctx_meta, name);
        if (!src) continue;

        bool convert = (do_f16 && src->type == GGML_TYPE_F32 && ggml_n_dims(src) >= 2);

        ggml_tensor * dst;
        if (convert) {
            dst = ggml_new_tensor(ctx_w, GGML_TYPE_F16, ggml_n_dims(src), src->ne);
        } else {
            dst = ggml_dup_tensor(ctx_w, src);
        }
        ggml_set_name(dst, name);
        tensors[name] = dst;
    }

    // Phase 2: Allocate backend buffer
    result.buffer = ggml_backend_alloc_ctx_tensors(ctx_w, backend);
    if (!result.buffer) {
        fprintf(stderr, "[fatal] backend buffer alloc failed\n");
        return result;
    }

    // Phase 3: Load data from file (with conversion if needed)
    FILE * f = fopen(path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "[fatal] reopen %s failed\n", path.c_str());
        ggml_backend_buffer_free(result.buffer);
        result.buffer = nullptr;
        return result;
    }

    const size_t data_off = gguf_get_data_offset(gctx);
    std::vector<uint8_t> read_buf;
    std::vector<uint8_t> conv_buf;

    for (int64_t i = 0; i < n_tensors; ++i) {
        const char * name = gguf_get_tensor_name(gctx, i);
        auto it = tensors.find(name);
        if (it == tensors.end()) continue;
        ggml_tensor * dst = it->second;

        ggml_tensor * src_meta = ggml_get_tensor(ctx_meta, name);
        size_t file_sz = ggml_nbytes(src_meta);  // Size on disk (F32)
        size_t dst_sz = ggml_nbytes(dst);         // Size in memory (possibly F16)

        size_t off = data_off + gguf_get_tensor_offset(gctx, i);

        if (src_meta->type == GGML_TYPE_F32 && dst->type == GGML_TYPE_F16) {
            // Read F32, convert to F16
            read_buf.resize(file_sz);
            fseek(f, (long)off, SEEK_SET);
            if (fread(read_buf.data(), 1, file_sz, f) != file_sz) {
                fprintf(stderr, "[fatal] read tensor '%s' failed\n", name);
                fclose(f);
                ggml_backend_buffer_free(result.buffer);
                result.buffer = nullptr;
                return result;
            }
            conv_buf.resize(dst_sz);
            size_t n_elements = (size_t)ggml_nelements(dst);
            convert_f32_to_f16((const float *)read_buf.data(), conv_buf.data(), n_elements);
            ggml_backend_tensor_set(dst, conv_buf.data(), 0, dst_sz);
        } else {
            // Direct copy
            read_buf.resize(file_sz);
            fseek(f, (long)off, SEEK_SET);
            if (fread(read_buf.data(), 1, file_sz, f) != file_sz) {
                fprintf(stderr, "[fatal] read tensor '%s' failed\n", name);
                fclose(f);
                ggml_backend_buffer_free(result.buffer);
                result.buffer = nullptr;
                return result;
            }
            ggml_backend_tensor_set(dst, read_buf.data(), 0, dst_sz);
        }
    }
    fclose(f);

    result.n_tensors = tensors.size();
    result.buffer_size_mb = ggml_backend_buffer_get_size(result.buffer) / (1024 * 1024);
    return result;
}

} // namespace dsrt
