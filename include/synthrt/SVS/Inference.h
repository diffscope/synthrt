#pragma once

#include <memory>
#include <string>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Task/ITask.h>
#include <synthrt/Core/srt_core_global.h>
#include <synthrt/SVS/srt_svs_global.h>

namespace srt::core {
    class Runtime;
}

namespace srt::svs {

    class InferenceSpec;

    class InferenceInitArgs : public core::TaskInitArgs {
    public:
        inline InferenceInitArgs(std::string name) : core::TaskInitArgs(std::move(name)) {
        }
        core::NO<core::ObjectPool> intermediateObjects;
    };

    class SRT_SVS_EXPORT Inference : public core::ITask {
    public:
        explicit Inference(const InferenceSpec *spec);
        ~Inference();

    public:
        const InferenceSpec *spec() const;
        core::Runtime *SU() const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

}
