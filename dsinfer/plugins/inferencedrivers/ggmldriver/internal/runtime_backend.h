#pragma once

#include <ggml-backend.h>
#include <ggml-cpu.h>
#include <ggml.h>
#include <gguf.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace dsrt {

void set_backend_mode(const char * mode);
ggml_backend_t init_backend(const char * component);

// Precision mode for weight loading (F32 = keep original, F16 = convert at load time)
// NOTE: F16 is only safe for feed-forward models (vocoder). Diffusion/reflow models
// (acoustic, variance, pitch) require F32 — ODE sampling amplifies rounding errors.
enum class Precision { F32, F16 };
void set_precision(Precision p);
Precision get_precision();

// Load tensors from a GGUF file into a ggml context with optional F32→F16 conversion.
// Populates `tensors` map and allocates backend buffer.
// Returns the weights buffer (caller owns it), or nullptr on failure.
// ctx_w must already be initialized with enough tensor overhead.
struct LoadResult {
    ggml_backend_buffer_t buffer = nullptr;
    size_t n_tensors = 0;
    size_t buffer_size_mb = 0;
};
LoadResult load_gguf_weights(
    const std::string & path,
    gguf_context * gctx,
    ggml_context * ctx_meta,
    ggml_context * ctx_w,
    ggml_backend_t backend,
    std::unordered_map<std::string, ggml_tensor *> & tensors);

} // namespace dsrt
