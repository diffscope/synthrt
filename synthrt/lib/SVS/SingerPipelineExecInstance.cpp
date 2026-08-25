#include "SingerPipelineExecInstance.h"

#include <utility>

#include "SingerContrib.h"

namespace srt {

    SingerPipelineExtension::SingerPipelineExtension(SingerSpec &spec, std::string id)
        : ContribSpecExtension(spec, std::move(id)) {
    }

    SingerPipelineExtension::~SingerPipelineExtension() = default;

    SingerPipelineExecInstance::SingerPipelineExecInstance(SingerSpec &spec)
        : ContribExecInstance(spec) {
    }

    SingerPipelineExecInstance::~SingerPipelineExecInstance() = default;

    Expected<void> SingerPipelineExecInstance::quit() {
        return {};
    }

    Expected<void> SingerPipelineExecInstance::wait() {
        return {};
    }

}
