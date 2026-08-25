#include "SingerExecInstance.h"

#include "ContribImportBinding.h"
#include "InferenceContrib.h"
#include "InferenceExecInstance.h"
#include "SingerContrib.h"

namespace srt {

    SingerExecInstance::SingerExecInstance(SingerSpec &spec) : ContribExecInstance(spec) {
    }

    SingerExecInstance::~SingerExecInstance() = default;

    SingerSpec &SingerExecInstance::spec() const {
        return *ContribExecInstance::spec().as<SingerSpec>();
    }

    Expected<std::unique_ptr<InferenceExecInstance>>
        SingerExecInstance::createInference(ContribImportBinding &binding,
                                            const InferenceRuntimeOptions &runtimeOptions) {
        if (&binding.importer() != &spec()) {
            return Error(Error::InvalidArgument,
                         "inference binding does not belong to this singer");
        }
        if (binding.state() != ContribImportBinding::Active) {
            return Error(Error::InvalidArgument, "inference binding is not active");
        }
        if (binding.target().locator().category() != "inference") {
            return Error(Error::InvalidArgument,
                         "singer import binding does not target an inference contribution");
        }
        return binding.target().as<InferenceSpec>()->createInference(binding.options(),
                                                                     runtimeOptions);
    }

    Expected<void> SingerExecInstance::quit() {
        return {};
    }

    Expected<void> SingerExecInstance::wait() {
        return {};
    }

}
