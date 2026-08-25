#include "InferenceContrib.h"

#include <set>
#include <string_view>

#include "InferenceInterpreter.h"
#include "InferenceInterpreterPlugin.h"

namespace srt {

    namespace {

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

        Expected<void> validateDeclaration(const JsonObject &declaration) {
            static const std::set<std::string_view> fields = {
                "configuration", "exports", "imports", "interface", "level", "name", "variant",
            };
            for (const auto &item : declaration) {
                if (fields.find(item.first) == fields.end()) {
                    return Error(Error::InvalidFormat,
                                 "inference declaration has an unknown field");
                }
            }
            return {};
        }

    }

    InferenceSpec::InferenceSpec(const ContribCreateContext &context) : ContribSpec(context) {
        m_declarationPath = *context.declarationPath();
    }

    InferenceSpec::~InferenceSpec() = default;

    const std::filesystem::path &InferenceSpec::declarationPath() const {
        return m_declarationPath;
    }

    Expected<std::unique_ptr<InferenceExecInstance>>
        InferenceSpec::createInference(const ContribImportOptions &importOptions,
                                       const InferenceRuntimeOptions &runtimeOptions) {
        auto *value = interpreter();
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
        : ContribCategory("inference", ModuleDeclaration, InferenceInterpreterPlugin::IID) {
    }

    InferenceCategory::~InferenceCategory() = default;

    std::vector<InferenceSpec *> InferenceCategory::inferences() const {
        std::vector<InferenceSpec *> result;
        const auto values = contributions();
        result.reserve(values.size());
        for (auto *value : values) {
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
        if (auto result = validateDeclaration(*context.manifestDeclaration()); !result) {
            return result.takeError();
        }
        return std::unique_ptr<ContribSpec>(new InferenceSpec(context));
    }

}

static srt::ContribCategoryRegistry::Add<srt::InferenceCategory>
    inferenceCategoryRegistration("inference", "");
