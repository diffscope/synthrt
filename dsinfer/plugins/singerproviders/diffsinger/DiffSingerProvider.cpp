#include "DiffSingerProvider.h"

#include <string_view>

#include <stdcorelib/path.h>

#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>
#include <synthrt/Core/PackageHandle.h>

#include "DiffSingerPipelineExecInstance.h"

namespace ds {

    namespace Ds = Api::DiffSinger::L1;

    namespace {

        struct InferenceBindings {
            srt::ContribImportBinding *duration = nullptr;
            srt::ContribImportBinding *pitch = nullptr;
            srt::ContribImportBinding *variance = nullptr;
            srt::ContribImportBinding *acoustic = nullptr;
            srt::ContribImportBinding *vocoder = nullptr;
            bool hasDuration = false;
            bool hasPitch = false;
            bool hasVariance = false;
            bool hasAcoustic = false;
            bool hasVocoder = false;
        };

        srt::Expected<InferenceBindings> collectInferenceBindings(const srt::ContribSpec &spec,
                                                                  bool requireBindings) {
            InferenceBindings result;
            const auto assign =
                [](const srt::ContribSpec &target, srt::ContribImportBinding *binding,
                   srt::ContribImportBinding **destination, bool *found,
                   std::string_view expectedInterface, std::string_view expectedVariant,
                   int expectedLevel) -> srt::Expected<void> {
                if (*found) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "DiffSinger declares a duplicate inference import");
                }
                if (target.interface() != expectedInterface ||
                    target.variant() != expectedVariant || target.level() != expectedLevel) {
                    return srt::Error(
                        srt::Error::InvalidFormat,
                        "DiffSinger inference import has an incompatible contract identity");
                }
                *found = true;
                *destination = binding;
                return {};
            };

            for (const auto &import : spec.imports()) {
                auto *binding = import.binding();
                if (requireBindings && !binding) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "DiffSinger import has no active runtime binding");
                }
                auto package = spec.package();
                auto *target = binding ? &binding->target() : package.resolve(import.locator());
                if (!target) {
                    return srt::Error(srt::Error::FileNotFound,
                                      "DiffSinger inference import target is unavailable");
                }
                const auto &role = import.role();
                srt::Expected<void> assigned;
                if (role == "duration") {
                    assigned = assign(*target, binding, &result.duration, &result.hasDuration,
                                      Api::Duration::L1::API_INTERFACE,
                                      Api::Duration::L1::API_VARIANT, Api::Duration::L1::API_LEVEL);
                } else if (role == "pitch") {
                    assigned = assign(*target, binding, &result.pitch, &result.hasPitch,
                                      Api::Pitch::L1::API_INTERFACE, Api::Pitch::L1::API_VARIANT,
                                      Api::Pitch::L1::API_LEVEL);
                } else if (role == "variance") {
                    assigned = assign(*target, binding, &result.variance, &result.hasVariance,
                                      Api::Variance::L1::API_INTERFACE,
                                      Api::Variance::L1::API_VARIANT, Api::Variance::L1::API_LEVEL);
                } else if (role == "acoustic") {
                    assigned = assign(*target, binding, &result.acoustic, &result.hasAcoustic,
                                      Api::Acoustic::L1::API_INTERFACE,
                                      Api::Acoustic::L1::API_VARIANT, Api::Acoustic::L1::API_LEVEL);
                } else if (role == "vocoder") {
                    assigned = assign(*target, binding, &result.vocoder, &result.hasVocoder,
                                      Api::Vocoder::L1::API_INTERFACE,
                                      Api::Vocoder::L1::API_VARIANT, Api::Vocoder::L1::API_LEVEL);
                } else {
                    continue;
                }
                if (!assigned) {
                    return assigned.takeError();
                }
            }
            if (!result.hasDuration || !result.hasPitch || !result.hasVariance ||
                !result.hasAcoustic || !result.hasVocoder) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "DiffSinger requires duration, pitch, variance, acoustic, and "
                                  "vocoder inference imports");
            }
            return result;
        }

    }

    static inline std::string formatErrorMessage(const std::string &msgPrefix,
                                                 const std::vector<std::string> &errorList);

    DiffSingerProvider::DiffSingerProvider() = default;

    DiffSingerProvider::~DiffSingerProvider() = default;

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        DiffSingerProvider::createConfiguration(const srt::ContribSpec &spec) const {
        const auto &singerSpec = *spec.as<srt::SingerSpec>();
        const auto &config = singerSpec.manifestConfiguration().toObject();
        auto result = std::make_unique<Ds::DiffSingerConfiguration>();

        // Collect all the errors and return to user
        bool hasErrors = false;
        std::vector<std::string> errorList;

        auto collectError = [&](auto &&msg) {
            hasErrors = true;
            errorList.emplace_back(std::forward<decltype(msg)>(msg));
        };

        // [REQUIRED] dict, path (json value is string)
        {
            static_assert(std::is_same_v<decltype(result->dict), std::filesystem::path>);
            if (const auto it = config.find("dict"); it != config.end()) {
                if (!it->second.isString()) {
                    collectError(R"(string field "dict" type mismatch)");
                } else {
                    result->dict =
                        stdc::path::clean_path(singerSpec.declarationPath().parent_path() /
                                               stdc::path::from_utf8(it->second.toString()));
                }
            } else {
                collectError(R"(string field "dict" is missing)");
            }
        } // dict

        if (hasErrors) {
            return srt::Error{
                srt::Error::InvalidFormat,
                formatErrorMessage("error parsing diffsinger configuration", errorList),
            };
        }
        return std::unique_ptr<srt::ContribConfiguration>(std::move(result));
    }

    srt::Expected<void> DiffSingerProvider::validateImports(const srt::ContribSpec &spec) const {
        auto result = collectInferenceBindings(spec, false);
        if (!result) {
            return result.takeError();
        }
        return {};
    }

    srt::Expected<std::unique_ptr<srt::SingerPipelineExecInstance>>
        DiffSingerProvider::createPipeline(srt::SingerSpec &spec,
                                           const srt::SingerPipelineRuntimeOptions &) {
        auto bindings = collectInferenceBindings(spec, true);
        if (!bindings) {
            return bindings.takeError();
        }
        return std::unique_ptr<srt::SingerPipelineExecInstance>(
            new DiffSingerPipelineExecInstance(spec));
    }

    static inline std::string formatErrorMessage(const std::string &msgPrefix,
                                                 const std::vector<std::string> &errorList) {
        const std::string middlePart = " (";
        const std::string countSuffix = " errors found):\n";

        size_t totalLength = msgPrefix.size() + middlePart.size() +
                             std::to_string(errorList.size()).size() + countSuffix.size();

        for (size_t i = 0; i < errorList.size(); ++i) {
            totalLength += std::to_string(i + 1).size() + 2; // index + ". "
            totalLength += errorList[i].size();
            if (i != errorList.size() - 1) {
                totalLength += 2; // "; "
            }
        }

        std::string result;
        result.reserve(totalLength);

        result.append(msgPrefix);
        result.append(middlePart);
        result.append(std::to_string(errorList.size()));
        result.append(countSuffix);

        for (size_t i = 0; i < errorList.size(); ++i) {
            result.append(std::to_string(i + 1));
            result.append(". ");
            result.append(errorList[i]);
            if (i != errorList.size() - 1) {
                result.append(";\n");
            }
        }

        return result;
    }

}
