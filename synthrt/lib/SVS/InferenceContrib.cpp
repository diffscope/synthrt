#include "InferenceContrib.h"

#include <set>
#include <string_view>
#include <utility>

#include "InferenceInterpreter.h"
#include "InferenceInterpreterPlugin.h"
#include "ContribExecutive.h"
#include "ContribImportBinding.h"
#include "Logging.h"

namespace srt {

    namespace {

        class InferenceExecutiveFactory : public ContribExecutiveFactory {
        public:
            explicit InferenceExecutiveFactory(ContribImportBinding &binding)
                : m_binding(&binding) {
            }

            Expected<std::unique_ptr<ContribExecutive>>
                create(const ContribRuntimeOptions &runtimeOptions) override {
                auto &target = m_binding->target();
                if (runtimeOptions.interface() != target.interface() ||
                    runtimeOptions.variant() != target.variant() ||
                    runtimeOptions.level() != target.level()) {
                    return Error(Error::InvalidArgument,
                                 "inference runtime options do not match the target contract");
                }
                auto result = target.as<InferenceSpec>()->createInference(
                    m_binding->options(), *runtimeOptions.as<InferenceRuntimeOptions>());
                if (!result) {
                    return result.takeError();
                }
                auto executive = result.take();
                if (&executive->spec() != &target) {
                    return Error(Error::InvalidFormat,
                                 "inference provider returned an executive for another target");
                }
                return std::unique_ptr<ContribExecutive>(std::move(executive));
            }

        private:
            ContribImportBinding *m_binding;
        };

        Expected<void> validateEntry(const JsonObject &entry) {
            static const std::set<std::string_view> fields = {"id", "path"};
            for (const auto &item : entry) {
                if (fields.find(item.first) == fields.end()) {
                    return Error(Error::InvalidFormat,
                                 "inference contribution entry has an unknown field");
                }
            }
            return {};
        }

    }

    InferenceSpec::InferenceSpec(const ContribCreateContext &context) : ContribSpec(context) {
    }

    InferenceSpec::~InferenceSpec() = default;

    Expected<void> InferenceSpec::validateCompatibilityWith(const InferenceSpec &other) const {
        auto value = interpreter();
        if (!value || !other.interpreter()) {
            return Error(Error::FeatureNotSupported,
                         "cannot validate compatibility for an inference that is not loaded");
        }
        return value->as<InferenceInterpreter>()->validateCompatibility(*this, other);
    }

    Expected<std::unique_ptr<InferenceExecutive>>
        InferenceSpec::createInference(const ContribImportOptions &importOptions,
                                       const InferenceRuntimeOptions &runtimeOptions) {
        auto value = interpreter();
        if (!value) {
            return Error(Error::FeatureNotSupported,
                         "cannot create inference from a contribution that is not loaded");
        }
        if (importOptions.interface() != interface() || importOptions.variant() != variant() ||
            importOptions.level() != level()) {
            return Error(Error::InvalidArgument,
                         "inference import options do not match the contribution contract");
        }
        if (runtimeOptions.interface() != interface() || runtimeOptions.variant() != variant() ||
            runtimeOptions.level() != level()) {
            return Error(Error::InvalidArgument,
                         "inference runtime options do not match the contribution contract");
        }
        return value->as<InferenceInterpreter>()->createInference(*this, importOptions,
                                                                  runtimeOptions);
    }

    InferenceCategory::InferenceCategory()
        : ContribCategory(NAME, ModuleDeclaration, InferenceInterpreterPlugin::IID) {
    }

    InferenceCategory::~InferenceCategory() = default;

    std::vector<InferenceSpec *> InferenceCategory::inferences() const {
        std::vector<InferenceSpec *> result;
        const auto values = contributions();
        result.reserve(values.size());
        for (auto value : values) {
            result.push_back(value->as<InferenceSpec>());
        }
        return result;
    }

    Expected<std::unique_ptr<ContribSpec>>
        InferenceCategory::createSpec(const ContribCreateContext &context) const {
        if (auto result = validateEntry(context.manifestEntry()); !result) {
            return result.takeError();
        }
        if (!context.manifestDeclaration() || !context.declarationPath()) {
            return Error(Error::InvalidFormat, "inference contribution requires a declaration");
        }
        // Unrecognized fields are retained and ignored, as the JSON profile of the specification
        // requires for every framework defined object. Each unrecognized field is logged at debug
        // level so that a misspelled optional field can be diagnosed.
        static const std::set<std::string_view> known = {
            "configuration", "exports", "imports", "interface", "level", "name", "variant",
        };
        for (const auto &item : *context.manifestDeclaration()) {
            if (known.find(item.first) == known.end()) {
                logCategory().srtDebug("inference declaration field \"%1\" is not recognized by "
                                       "this runtime and is ignored",
                                       item.first);
            }
        }
        return std::unique_ptr<ContribSpec>(new InferenceSpec(context));
    }

    Expected<std::unique_ptr<ContribExecutiveFactory>>
        InferenceCategory::createExecutiveFactory(ContribImportBinding &binding) const {
        return std::unique_ptr<ContribExecutiveFactory>(new InferenceExecutiveFactory(binding));
    }

}

static srt::ContribCategoryRegistry::Add<srt::InferenceCategory>
    inferenceCategoryRegistration(srt::InferenceCategory::NAME, "");
