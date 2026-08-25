#include "SingerPipelineExecutive.h"

#include <utility>

#include "SingerContrib.h"

namespace srt {

    SingerPipelineExtension::SingerPipelineExtension(SingerSpec &spec, std::string id)
        : ContribSpecExtension(spec, std::move(id)) {
    }

    SingerPipelineExtension::~SingerPipelineExtension() = default;

    SingerPipelineExecutive::SingerPipelineExecutive(SingerSpec &spec) : ContribExecutive(spec) {
    }

    SingerPipelineExecutive::~SingerPipelineExecutive() = default;

    Expected<void> SingerPipelineExecutive::quit() {
        return {};
    }

    Expected<void> SingerPipelineExecutive::wait() {
        return {};
    }

}
