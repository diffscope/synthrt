// OnnxSetup.cpp - Setup ONNX inference driver in a Runtime.
//
// Extracted from DiffSingerSession::initialize() Stage 5 (L531-L600) as part
// of v1 Phase 3 P0-c. This function registers plugin paths, loads the "onnx"
// InferenceDriverPlugin, creates and initializes an OnnxDriver, and registers
// it in the Runtime's "inference" category as "dsdriver".

#include <synthrt/Driver/OnnxSetup.h>

#include <string>

#include <stdcorelib/path.h>

#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Plugin/PluginFactory.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceDriverPlugin.h>

namespace srt::driver {

    srt::core::Expected<void> setupOnnxInferenceDriver(
        srt::core::Runtime &runtime,
        const std::filesystem::path &pluginRoot,
        const OnnxDriverConfig &config) {

        const auto driverPluginDir =
            pluginRoot / stdc::path::from_utf8("srt-driver") /
            stdc::path::from_utf8("inferencedrivers");
        const auto diffsingerPluginDir =
            pluginRoot / stdc::path::from_utf8("diffsinger");
        const auto singerProviderDir =
            diffsingerPluginDir / stdc::path::from_utf8("singerproviders");
        const auto interpreterDir =
            diffsingerPluginDir / stdc::path::from_utf8("inferenceinterpreters");

        // 1. Register plugin search paths for singer providers, the inference
        //    driver, and the 5 inference interpreters. Each uses a dedicated
        //    IID but shares parent plugin directories.
        auto *plugins = runtime.services().get<srt::core::PluginFactory>();
        if (!plugins) {
            return srt::core::Error(
                srt::core::Error::SessionError,
                "setupOnnxInferenceDriver: plugin service is not available");
        }

        plugins->addPluginPath("srt.svs.singer-provider.diffsinger", singerProviderDir);
        plugins->addPluginPath("srt.driver.InferenceDriver", driverPluginDir);
        plugins->addPluginPath("srt.svs.interpreter.acoustic", interpreterDir);
        plugins->addPluginPath("srt.svs.interpreter.duration", interpreterDir);
        plugins->addPluginPath("srt.svs.interpreter.pitch", interpreterDir);
        plugins->addPluginPath("srt.svs.interpreter.variance", interpreterDir);
        plugins->addPluginPath("srt.svs.interpreter.vocoder", interpreterDir);

        // 2. Load the "onnx" InferenceDriverPlugin.
        auto plugin = plugins->plugin<srt::driver::InferenceDriverPlugin>("onnx");
        if (!plugin) {
            return srt::core::Error(
                srt::core::Error::SessionError,
                "setupOnnxInferenceDriver: failed to load inference driver plugin 'onnx'");
        }

        // 3. Create OnnxDriver and initialize it.
        auto onnxDriver = plugin->create();
        if (!onnxDriver) {
            return srt::core::Error(
                srt::core::Error::SessionError,
                "setupOnnxInferenceDriver: failed to create onnx driver instance");
        }

        auto onnxArgs =
            srt::core::NO<srt::driver::onnx::DriverInitArgs>::create();
        onnxArgs->ep = config.ep;
        onnxArgs->deviceIndex = config.deviceIndex;

        // Derive onnxruntime library path relative to the plugin's location:
        //   <pluginDir>/runtimes/onnx/{cuda|default}/
        const auto ortParentPath =
            plugin->path().parent_path() /
            stdc::path::from_utf8("runtimes") /
            stdc::path::from_utf8("onnx");
        if (config.ep == srt::driver::onnx::ExecutionProvider::CUDAExecutionProvider) {
            onnxArgs->runtimePath = ortParentPath / stdc::path::from_utf8("cuda");
        } else {
            onnxArgs->runtimePath = ortParentPath / stdc::path::from_utf8("default");
        }

        if (auto exp = onnxDriver->initialize(onnxArgs); !exp) {
            return srt::core::Error(
                srt::core::Error::SessionError,
                "setupOnnxInferenceDriver: failed to initialize onnx driver: " +
                    exp.error().message());
        }

        // 4. Register the driver under the "inference" category with id
        //    "dsdriver" so inference interpreters can discover it.
        auto *inferenceCat = runtime.moduleCategory("inference");
        if (!inferenceCat) {
            return srt::core::Error(
                srt::core::Error::SessionError,
                "setupOnnxInferenceDriver: inference module category is not available");
        }
        inferenceCat->addObject("dsdriver", onnxDriver);

        return srt::core::Expected<void>{};
    }

} // namespace srt::driver
