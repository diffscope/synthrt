#include "DiffSingerProvider.h"

#include <string_view>

#include <stdcorelib/adt/vlarray.h>
#include <stdcorelib/path.h>

#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>
#include <synthrt/Core/ContribImportBinding.h>
#include <synthrt/SVS/InferenceContrib.h>

#include "DiffSingerPipelineExecutive.h"

namespace ds {

    namespace Ds = Api::DiffSinger::L1;

    namespace {

        bool isDiffSingerSpec(const srt::ContribSpec &spec) {
            return spec.locator().category() == "singer" && spec.interface() == Ds::API_INTERFACE &&
                   spec.variant() == Ds::API_VARIANT && spec.level() == Ds::API_LEVEL;
        }

        srt::Expected<void> validateKnownImports(const srt::ContribSpec &spec);

        class DiffSingerImportValidator : public srt::ContribImportValidator {
        public:
            srt::Expected<void> validateImports(const srt::ContribSpec &spec) const override {
                if (!isDiffSingerSpec(spec)) {
                    return {};
                }
                return validateKnownImports(spec);
            }
        };

        class DiffSingerPipelineExtension : public srt::SingerPipelineExtension {
        public:
            explicit DiffSingerPipelineExtension(srt::SingerSpec &spec)
                : SingerPipelineExtension(
                      spec, srt::ContribSpecExtensionTraits<srt::SingerSpec,
                                                            Ds::DiffSingerPipelineExecutive>::ID) {
            }

            srt::Expected<std::unique_ptr<srt::SingerPipelineExecutive>>
                createPipeline(const srt::SingerPipelineRuntimeOptions &runtimeOptions) override {
                if (runtimeOptions.interface() != Ds::API_INTERFACE ||
                    runtimeOptions.variant() != Ds::API_VARIANT ||
                    runtimeOptions.level() != Ds::API_LEVEL) {
                    return srt::Error(
                        srt::Error::InvalidArgument,
                        "DiffSinger pipeline options have an incompatible contract identity");
                }
                return std::unique_ptr<srt::SingerPipelineExecutive>(
                    new DiffSingerPipelineExecutive(spec()));
            }
        };

        srt::Expected<srt::InferenceSpec *> resolveKnownImport(const srt::ContribSpec &spec,
                                                               std::string_view role,
                                                               std::string_view expectedInterface,
                                                               std::string_view expectedVariant,
                                                               int expectedLevel, bool required) {
            const auto import = spec.findImport(role);
            if (!import) {
                if (required) {
                    return srt::Error(srt::Error::InvalidFormat, "DiffSinger requires the " +
                                                                     std::string(role) +
                                                                     " inference import");
                }
                return static_cast<srt::InferenceSpec *>(nullptr);
            }
            if (!import->binding()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "DiffSinger inference import has no prepared binding");
            }
            if (!import->executiveFactory()) {
                return srt::Error(srt::Error::FeatureNotSupported,
                                  "DiffSinger inference import has no execution factory");
            }
            auto target = &import->binding()->target();
            if (target->locator().category() != "inference" ||
                target->interface() != expectedInterface || target->variant() != expectedVariant ||
                target->level() != expectedLevel) {
                return srt::Error(
                    srt::Error::InvalidFormat,
                    "DiffSinger inference import has an incompatible contract identity");
            }
            return target->as<srt::InferenceSpec>();
        }

        srt::Expected<void> validateKnownImports(const srt::ContribSpec &spec) {
            auto duration = resolveKnownImport(
                spec, "singer/duration", Api::Duration::L1::API_INTERFACE,
                Api::Duration::L1::API_VARIANT, Api::Duration::L1::API_LEVEL, false);
            if (!duration) {
                return duration.takeError();
            }
            auto pitch =
                resolveKnownImport(spec, "singer/pitch", Api::Pitch::L1::API_INTERFACE,
                                   Api::Pitch::L1::API_VARIANT, Api::Pitch::L1::API_LEVEL, false);
            if (!pitch) {
                return pitch.takeError();
            }
            auto variance = resolveKnownImport(
                spec, "singer/variance", Api::Variance::L1::API_INTERFACE,
                Api::Variance::L1::API_VARIANT, Api::Variance::L1::API_LEVEL, false);
            if (!variance) {
                return variance.takeError();
            }
            auto acoustic = resolveKnownImport(
                spec, "singer/acoustic", Api::Acoustic::L1::API_INTERFACE,
                Api::Acoustic::L1::API_VARIANT, Api::Acoustic::L1::API_LEVEL, true);
            if (!acoustic) {
                return acoustic.takeError();
            }
            auto vocoder = resolveKnownImport(
                spec, "singer/vocoder", Api::Vocoder::L1::API_INTERFACE,
                Api::Vocoder::L1::API_VARIANT, Api::Vocoder::L1::API_LEVEL, true);
            if (!vocoder) {
                return vocoder.takeError();
            }
            auto compatibility = (*vocoder)->validateCompatibilityWith(**acoustic);
            if (!compatibility) {
                return compatibility.takeError().withContext(
                    "DiffSinger vocoder import is incompatible with its acoustic import");
            }
            return {};
        }

    }

    static inline std::string formatErrorMessage(const std::string &msgPrefix,
                                                 const stdc::vlarray<std::string> &errorList);

    DiffSingerProvider::DiffSingerProvider() = default;

    DiffSingerProvider::~DiffSingerProvider() = default;

    srt::Expected<std::vector<std::unique_ptr<srt::ContribImportValidator>>>
        DiffSingerProvider::createImportValidators() const {
        std::vector<std::unique_ptr<srt::ContribImportValidator>> result;
        result.emplace_back(new DiffSingerImportValidator());
        return result;
    }

    srt::Expected<std::vector<std::unique_ptr<srt::ContribSpecExtension>>>
        DiffSingerProvider::createExtensions(srt::ContribSpec &spec) const {
        std::vector<std::unique_ptr<srt::ContribSpecExtension>> result;
        if (isDiffSingerSpec(spec)) {
            result.emplace_back(new DiffSingerPipelineExtension(*spec.as<srt::SingerSpec>()));
        }
        return result;
    }

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        DiffSingerProvider::createConfiguration(const srt::ContribSpec &spec) const {
        const auto &singerSpec = *spec.as<srt::SingerSpec>();
        const auto &config = singerSpec.manifestConfiguration().toObject();
        auto result = std::make_unique<Ds::DiffSingerConfiguration>();

        // Collect all the errors and return to user
        bool hasErrors = false;
        stdc::vlarray<std::string> errorList;

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

    static inline std::string formatErrorMessage(const std::string &msgPrefix,
                                                 const stdc::vlarray<std::string> &errorList) {
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
