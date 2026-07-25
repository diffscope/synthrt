#pragma once

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Extract/srt_extract_global.h>

namespace srt::core {
    class Runtime;
}

namespace srt::extract {

    /// 从 Runtime 获取 ONNX 推理驱动
    ///
    /// 提取器插件用此函数获取已注册的 dsdriver。
    /// 只检查 backend == "onnx"，不检查 arch（提取器不是 DiffSinger 模型）。
    SRT_EXTRACT_EXPORT
    srt::core::Expected<srt::core::NO<srt::driver::InferenceDriver>>
    getInferenceDriver(const srt::core::Runtime *runtime);

} // namespace srt::extract
