#pragma once

#include <synthrt/SVS/InferenceInterpreter.h>
#include <synthrt/Core/Plugin/Plugin.h>
#include <synthrt/SVS/srt_svs_global.h>

namespace srt::svs {

    class SRT_SVS_EXPORT InferenceInterpreterPlugin : public core::Plugin {
    public:
        virtual core::NO<InferenceInterpreter> create() = 0;
    };

}
