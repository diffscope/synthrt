#include "SingerPipelineExecInstance.h"

#include "SingerContrib.h"

namespace srt {

    SingerPipelineExecInstance::SingerPipelineExecInstance(SingerSpec &spec)
        : ContribExecInstance(spec) {
    }

    SingerPipelineExecInstance::~SingerPipelineExecInstance() = default;

    SingerSpec &SingerPipelineExecInstance::spec() const {
        return *ContribExecInstance::spec().as<SingerSpec>();
    }

    Expected<void> SingerPipelineExecInstance::quit() {
        return {};
    }

    Expected<void> SingerPipelineExecInstance::wait() {
        return {};
    }

}
