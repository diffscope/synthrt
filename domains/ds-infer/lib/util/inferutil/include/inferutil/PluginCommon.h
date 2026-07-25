#pragma once

#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>

#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Task/ITask.h>
#include <synthrt/SVS/Inference.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>

/// \file PluginCommon.h
/// \brief DiffSinger 推理插件共享的模板化参数校验、config 获取与流程级工具。
///
/// 5 个推理插件（acoustic/duration/pitch/variance/vocoder）的 .cpp 中
/// getConfig / initialize 前置校验 / start 前置校验 / driver 检查 / session
/// 打开 / frameWidth 校验等模板高度一致（CODING-05 >60% 重叠）。本头文件
/// 提取为模板/内联函数，避免重复实现。
///
/// 兼容性：
/// - D-11：仅提取内部实现，不动 srt::svs::Inference 公共类签名
/// - ARCH-01：不引入新职责，仅工具函数组合
/// - ARCH-03：组合优于继承，不引入中间基类
/// - CODING-04：工具函数不含 mutex 加锁，由调用方显式控制
/// - ROBUST-03：所有指针/句柄参数均防空
/// - ROBUST-05：错误消息保留 logPrefix，不丢失上下文

namespace ds::infer::inferutil {

    /// 获取并校验 InferenceSpec 上的 typed configuration。
    ///
    /// dsinfer API 中 `API_CLASS` 与 `API_NAME` 为命名空间级 `inline constexpr char[]`
    /// 常量（非类静态成员），故以参数形式传入。模板参数 T 仅约束返回类型。
    ///
    /// \param spec       InferenceSpec 指针，可为空（将返回错误）
    /// \param apiClass   期望的 className（如 Ac::API_CLASS = "ai.svs.AcousticInference"）
    /// \param apiName    期望的 objectName（如 Ac::API_NAME = "acoustic"）
    /// \param logPrefix  错误消息前缀，如 "[Acoustic]"
    /// \return 成功返回 NO<T>，失败返回 InvalidArgument 错误
    template <typename T>
    srt::core::Expected<srt::core::NO<T>>
        getTypedConfig(const srt::svs::InferenceSpec *spec,
                       std::string_view apiClass,
                       std::string_view apiName,
                       std::string_view logPrefix) {
        if (!spec) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                std::string(logPrefix) + " inference spec is nullptr");
        }
        const auto genericConfig = spec->configuration();
        if (!genericConfig) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                std::string(logPrefix) + " configuration is nullptr");
        }
        if (!(genericConfig->className() == apiClass &&
              genericConfig->objectName() == apiName)) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                std::string(logPrefix) + " invalid configuration class/name");
        }
        return genericConfig.template as<T>();
    }

    /// 获取并校验 InferenceSpec 上的 typed schema。
    /// 与 getTypedConfig 对称，但调用 spec->schema()。当前仅 VarianceInference 使用。
    /// 修复 ROBUST-03：原 VarianceInference 本地 getSchema 未检查 spec 非空。
    ///
    /// \param spec       InferenceSpec 指针，可为空（将返回错误）
    /// \param apiClass   期望的 className
    /// \param apiName    期望的 objectName
    /// \param logPrefix  错误消息前缀，如 "[Variance]"
    /// \return 成功返回 NO<T>，失败返回 InvalidArgument 错误
    template <typename T>
    srt::core::Expected<srt::core::NO<T>>
        getTypedSchema(const srt::svs::InferenceSpec *spec,
                       std::string_view apiClass,
                       std::string_view apiName,
                       std::string_view logPrefix) {
        if (!spec) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                std::string(logPrefix) + " inference spec is nullptr");
        }
        const auto genericSchema = spec->schema();
        if (!genericSchema) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                std::string(logPrefix) + " schema is nullptr");
        }
        if (!(genericSchema->className() == apiClass &&
              genericSchema->objectName() == apiName)) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                std::string(logPrefix) + " invalid schema class/name");
        }
        return genericSchema.template as<T>();
    }

    /// 校验 Inference::initialize() 的 args 参数。
    ///
    /// 成功返回 Expected<void>，失败返回带 logPrefix 的 InvalidArgument 错误。
    /// 本函数不调用 setState(Failed)——状态机由调用方在错误路径上自行设置，
    /// 避免工具函数与 ITask 状态机耦合（ARCH-01）。
    ///
    /// \param args       TaskInitArgs 句柄
    /// \param apiName    期望的 API_NAME（如 Acoustic::API_NAME）
    /// \param logPrefix  错误消息前缀，如 "[Acoustic]"
    inline srt::core::Expected<void>
        validateInitArgs(const srt::core::NO<srt::core::TaskInitArgs> &args,
                         std::string_view apiName,
                         std::string_view logPrefix) {
        if (!args) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                std::string(logPrefix) + " task init args is nullptr");
        }
        if (auto name = args->objectName(); name != apiName) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                stdc::formatN(
                    R"(%1 invalid task init args name: expected "%2", got "%3")",
                    std::string(logPrefix), std::string(apiName), name));
        }
        return srt::core::Expected<void>();
    }

    /// 校验 Inference::start() 的 input 参数。
    ///
    /// 成功返回 Expected<void>，失败返回带 logPrefix 的 InvalidArgument 错误。
    /// 本函数不调用 setState(Failed)——状态机由调用方在错误路径上自行设置，
    /// 避免工具函数与 ITask 状态机耦合（ARCH-01）。
    ///
    /// \param input      TaskStartInput 句柄
    /// \param apiName    期望的 API_NAME（如 Acoustic::API_NAME）
    /// \param logPrefix  错误消息前缀，如 "[Acoustic]"
    inline srt::core::Expected<void>
        validateStartInput(const srt::core::NO<srt::core::TaskStartInput> &input,
                           std::string_view apiName,
                           std::string_view logPrefix) {
        if (!input) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                std::string(logPrefix) + " start: input is nullptr");
        }
        if (const auto &name = input->objectName(); name != apiName) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                stdc::formatN(
                    R"(%1 start: invalid input name: expected "%2", got "%3")",
                    std::string(logPrefix), std::string(apiName), name));
        }
        return srt::core::Expected<void>();
    }

    /// 校验 InferenceDriver 是否已初始化。
    ///
    /// 用于 start() 开头的前置检查。本函数不调用 setState(Failed)——状态机
    /// 由调用方在错误路径上自行设置（ARCH-01）。本函数不包含 mutex 加锁——
    /// 调用方需在 shared_lock 内调用（CODING-04 线程安全显式控制）。
    ///
    /// \param driver     InferenceDriver 句柄
    /// \param logPrefix  错误消息前缀，如 "[Acoustic]"
    /// \return 成功返回 void，失败返回 InferenceStartFailed 错误
    inline srt::core::Expected<void>
        checkDriverReady(const srt::core::NO<srt::driver::InferenceDriver> &driver,
                         std::string_view logPrefix) {
        if (!driver) {
            return srt::core::Error(
                srt::core::ErrorCode::InferenceStartFailed,
                std::string(logPrefix) + " inference driver not initialized");
        }
        return srt::core::Expected<void>();
    }

    /// 创建并打开一个 ONNX InferenceSession。
    ///
    /// 用于 initialize() 中打开 encoder/predictor session。本函数不调用
    /// setState(Failed)（ARCH-01）。本函数不包含 mutex 加锁——调用方需在
    /// unique_lock 内调用（CODING-04）。
    ///
    /// \param driver       InferenceDriver 句柄
    /// \param modelPath    ONNX 模型路径
    /// \param useCpu       是否使用 CPU（通常 false，用 GPU）
    /// \param sessionName  session 名称，用于错误消息（"encoder"/"predictor"/"session"）
    /// \param logPrefix    错误消息前缀，如 "[Acoustic]"
    /// \return 成功返回 NO<InferenceSession>，失败返回 Error
    inline srt::core::Expected<srt::core::NO<srt::driver::InferenceSession>>
        openOnnxSession(const srt::core::NO<srt::driver::InferenceDriver> &driver,
                        const std::filesystem::path &modelPath,
                        bool useCpu,
                        std::string_view sessionName,
                        std::string_view logPrefix) {
        if (!driver) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                std::string(logPrefix) + " openOnnxSession: driver is nullptr");
        }
        auto session = driver->createSession();
        auto openArgs = srt::core::NO<srt::driver::onnx::SessionOpenArgs>::create();
        openArgs->useCpu = useCpu;
        if (auto res = session->open(modelPath, openArgs); !res) {
            return srt::core::Error(
                srt::core::ErrorCode::InferenceStartFailed,
                stdc::formatN("%1 initialize: failed to open %2 session for model %3",
                              std::string(logPrefix), std::string(sessionName),
                              stdc::path::to_utf8(modelPath)));
        }
        return session;
    }

    /// 校验 frameWidth 为正有限数。
    ///
    /// 用于 start() 中校验 config->frameWidth。本函数不调用 setState(Failed)
    /// （ARCH-01）。
    ///
    /// \param frameWidth  config->frameWidth 值
    /// \param logPrefix   错误消息前缀，如 "[Duration]"
    /// \return 成功返回 void，失败返回 InvalidArgument 错误
    inline srt::core::Expected<void>
        validateFrameWidth(double frameWidth, std::string_view logPrefix) {
        if (!std::isfinite(frameWidth) || frameWidth <= 0) {
            return srt::core::Error(
                srt::core::ErrorCode::InvalidArgument,
                std::string(logPrefix) + " frame width must be positive");
        }
        return srt::core::Expected<void>();
    }

}

