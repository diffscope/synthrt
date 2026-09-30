#include "DiffSingerProvider.h"

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <stdcorelib/adt/vlarray.h>
#include <stdcorelib/path.h>

#include <dsinfer/Api/Singers/DiffSinger/1/DiffSingerApiL1.h>
#include <synthrt/Core/ContribImportBinding.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/SingerContrib.h>

#include "DiffSingerPipelineExecutive.h"

namespace ds {

    namespace Ds = Api::DiffSinger::L1;

    // Declared before the anonymous namespace because the import validation in that namespace
    // also uses it, and the definition appears later in this file.
    static inline std::string formatErrorMessage(const std::string &msgPrefix,
                                                 const stdc::vlarray<std::string> &errorList);

    namespace {

        bool isDiffSingerSpec(const srt::ContribSpec &spec) {
            return spec.locator().category() == srt::SingerCategory::NAME &&
                   spec.interface() == Ds::API_INTERFACE && spec.variant() == Ds::API_VARIANT &&
                   spec.level() == Ds::API_LEVEL;
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
            if (target->locator().category() != srt::InferenceCategory::NAME ||
                target->interface() != expectedInterface || target->variant() != expectedVariant ||
                target->level() != expectedLevel) {
                return srt::Error(
                    srt::Error::InvalidFormat,
                    "DiffSinger inference import has an incompatible contract identity");
            }
            return target->as<srt::InferenceSpec>();
        }

        // Returns the reserved phonemes that are absent from the phoneme table of \a model.
        // Returns an empty list if \a model is null, \a reserved is empty, or the model has no
        // configuration.
        //
        // The function is templated on the configuration type because the four configuration
        // types that carry a phoneme table share no base class that exposes it. Each of them names
        // the table \c phonemes, which is the only requirement on \a Configuration.
        template <class Configuration>
        std::vector<std::string> missingFrom(const srt::InferenceSpec *model,
                                             const std::vector<std::string> &reserved) {
            std::vector<std::string> missing;
            if (model == nullptr || reserved.empty()) {
                return missing;
            }
            const auto *configuration = static_cast<const Configuration *>(model->configuration());
            if (configuration == nullptr) {
                return missing;
            }
            for (const auto &token : reserved) {
                if (configuration->phonemes.find(token) == configuration->phonemes.end()) {
                    missing.push_back(token);
                }
            }
            return missing;
        }

        std::string listOf(const std::vector<std::string> &values) {
            std::string result;
            for (const auto &value : values) {
                result += (result.empty() ? "" : " ") + value;
            }
            return result;
        }

        // Verifies that every reserved phoneme of the singer is present in the phoneme table of
        // each imported model.
        //
        // A reserved phoneme bypasses phoneme conversion because hosts pass the token directly to
        // the models. This function is therefore the only point at which the token is validated.
        // Without this check, a singer that reserves a token absent from its models loads and
        // synthesizes successfully but produces silence where the user wrote the marker, and no
        // later stage detects the error.
        //
        // Every model with a phoneme table is checked, not only the acoustic model. The tables
        // are separate files. If they disagree, a marker can be valid for the duration of a note
        // and invalid for its synthesized sound.
        srt::Expected<void> validateReservedPhonemes(const srt::ContribSpec &spec,
                                                     srt::InferenceSpec *duration,
                                                     srt::InferenceSpec *pitch,
                                                     srt::InferenceSpec *variance,
                                                     srt::InferenceSpec *acoustic) {
            // The singer category has already parsed the declaration, so every host and language
            // library reads the same set. This provider adds only the check against the phoneme
            // tables of the models.
            const auto &reserved = spec.as<srt::SingerSpec>()->reservedPhonemes();
            if (reserved.empty()) {
                return {};
            }

            stdc::vlarray<std::string> errorList;
            const auto check = [&](const char *what, std::vector<std::string> missing) {
                if (!missing.empty()) {
                    errorList.emplace_back("missing from the " + std::string(what) +
                                           " model: " + listOf(missing));
                }
            };
            check("duration",
                  missingFrom<Api::Duration::L1::DurationConfiguration>(duration, reserved));
            check("pitch", missingFrom<Api::Pitch::L1::PitchConfiguration>(pitch, reserved));
            check("variance",
                  missingFrom<Api::Variance::L1::VarianceConfiguration>(variance, reserved));
            check("acoustic",
                  missingFrom<Api::Acoustic::L1::AcousticConfiguration>(acoustic, reserved));
            if (errorList.empty()) {
                return {};
            }
            return srt::Error{
                srt::Error::InvalidFormat,
                formatErrorMessage("DiffSinger singer declares reserved phonemes that are "
                                   "missing from its models",
                                   errorList),
            };
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
            return validateReservedPhonemes(spec, *duration, *pitch, *variance, *acoustic);
        }

    }

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
