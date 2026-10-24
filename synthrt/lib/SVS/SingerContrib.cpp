#include "SingerContrib.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <stdcorelib/path.h>

#include "Logging.h"
#include "SingerPipelineExecutive.h"
#include "SingerProviderPlugin.h"

namespace fs = std::filesystem;

namespace srt {

    namespace {

        Expected<void> validateEntry(const JsonObject &entry) {
            static const std::set<std::string_view> fields = {"id", "path"};
            for (const auto &item : entry) {
                if (fields.find(item.first) == fields.end()) {
                    return Error(Error::InvalidFormat,
                                 "singer contribution entry has an unknown field");
                }
            }
            return {};
        }

        // Reads the language map and the default language, which the category adds to every
        // singer.
        //
        // Roles are checked only against the imports of this declaration. The language category
        // is responsible for rejecting a handle that is not a language, or a role whose import is
        // not a language, because the singer category does not define languages.
        Expected<void> readLanguages(const JsonObject &declaration,
                                     stdc::array_view<ContribImport> imports,
                                     std::map<std::string, std::string> *languages,
                                     std::string *defaultLanguage) {
            const auto languagesIt = declaration.find("languages");
            if (languagesIt != declaration.end()) {
                if (!languagesIt->second.isObject()) {
                    return Error(Error::InvalidFormat,
                                 "singer languages must map language handles to import roles");
                }
                for (const auto &[handle, roleValue] : languagesIt->second.toObject()) {
                    if (handle.empty() || !roleValue.isString() || roleValue.toString().empty()) {
                        return Error(Error::InvalidFormat,
                                     "singer languages must map language handles to import roles");
                    }
                    const auto role = roleValue.toString();
                    const auto known = std::any_of(
                        imports.begin(), imports.end(),
                        [&role](const ContribImport &item) { return item.role() == role; });
                    if (!known) {
                        return Error(Error::InvalidFormat,
                                     "singer language " + handle +
                                         " refers to a nonexistent import role: " + role);
                    }
                    languages->emplace(handle, role);
                }
            }
            const auto defaultIt = declaration.find("defaultLanguage");
            if (defaultIt != declaration.end()) {
                if (!defaultIt->second.isString()) {
                    return Error(Error::InvalidFormat, "singer defaultLanguage must be a string");
                }
                *defaultLanguage = defaultIt->second.toString();
                if (languages->find(*defaultLanguage) == languages->end()) {
                    return Error(Error::InvalidFormat,
                                 "singer defaultLanguage is not a key of languages: " +
                                     *defaultLanguage);
                }
            } else if (!languages->empty()) {
                // A JSON object orders its members by key, so the declaration order of the
                // languages is not preserved and the default language must be declared
                // explicitly.
                return Error(Error::InvalidFormat,
                             "a singer that declares languages must declare defaultLanguage");
            }
            return {};
        }

        // Reads the reserved phonemes and validates only that they are distinct non-empty
        // strings. The singer validator checks that the models contain them.
        Expected<void> readReservedPhonemes(const JsonObject &declaration,
                                            std::vector<std::string> *reserved) {
            const auto it = declaration.find("reservedPhonemes");
            if (it == declaration.end()) {
                return {};
            }
            if (!it->second.isArray()) {
                return Error(Error::InvalidFormat, "singer reservedPhonemes must be an array");
            }
            std::set<std::string> seen;
            for (const auto &item : it->second.toArray()) {
                if (!item.isString() || item.toString().empty()) {
                    return Error(Error::InvalidFormat,
                                 "singer reservedPhonemes must contain only non-empty strings");
                }
                auto token = item.toString();
                if (!seen.insert(token).second) {
                    return Error(Error::InvalidFormat,
                                 "singer reservedPhonemes contains " + token + " twice");
                }
                reserved->push_back(std::move(token));
            }
            return {};
        }

        fs::path resolvePath(const fs::path &base, const std::string &text) {
            std::string normalized = text;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            auto path = stdc::path::from_utf8(normalized);
            if (path.is_relative()) {
                path = base / path;
            }
            return path.lexically_normal();
        }

        Expected<DisplayText> readDisplayPath(const JsonValue &value, std::string_view field,
                                              const fs::path &base) {
            const auto convert = [&base, field](const std::string &text) -> Expected<std::string> {
                if (text.find('\0') != std::string::npos) {
                    return Error(Error::InvalidFormat,
                                 std::string(field) + " path must not contain NUL");
                }
                return stdc::path::to_utf8(resolvePath(base, text));
            };
            if (value.isString()) {
                auto converted = convert(value.toString());
                if (!converted) {
                    return converted.takeError();
                }
                return DisplayText(converted.take());
            }
            if (!value.isObject()) {
                return Error(Error::InvalidFormat,
                             std::string(field) + " must be a string or language map");
            }
            const auto &object = value.toObject();
            const auto defaultIt = object.find("_");
            if (defaultIt == object.end() || !defaultIt->second.isString()) {
                return Error(Error::InvalidFormat,
                             std::string(field) + " language map requires a string _ field");
            }
            std::map<std::string, std::string> localized;
            for (const auto &item : object) {
                if (!item.second.isString()) {
                    return Error(Error::InvalidFormat,
                                 std::string(field) + " language map values must be strings");
                }
                if (item.first != "_") {
                    auto converted = convert(item.second.toString());
                    if (!converted) {
                        return converted.takeError();
                    }
                    localized.emplace(item.first, converted.take());
                }
            }
            auto defaultText = convert(defaultIt->second.toString());
            if (!defaultText) {
                return defaultText.takeError();
            }
            return DisplayText(defaultText.take(), localized);
        }

    }

    SingerSpec::SingerSpec(const ContribCreateContext &context, DisplayText avatar,
                           DisplayText background, DisplayText demoAudio,
                           std::map<std::string, std::string> languages,
                           std::string defaultLanguage, std::vector<std::string> reservedPhonemes)
        : ContribSpec(context), m_avatar(std::move(avatar)), m_background(std::move(background)),
          m_demoAudio(std::move(demoAudio)), m_languages(std::move(languages)),
          m_defaultLanguage(std::move(defaultLanguage)),
          m_reservedPhonemes(std::move(reservedPhonemes)) {
    }

    SingerSpec::~SingerSpec() = default;

    const DisplayText &SingerSpec::avatar() const {
        return m_avatar;
    }

    const DisplayText &SingerSpec::background() const {
        return m_background;
    }

    const DisplayText &SingerSpec::demoAudio() const {
        return m_demoAudio;
    }

    const std::map<std::string, std::string> &SingerSpec::languages() const {
        return m_languages;
    }

    const std::string &SingerSpec::defaultLanguage() const {
        return m_defaultLanguage;
    }

    const std::vector<std::string> &SingerSpec::reservedPhonemes() const {
        return m_reservedPhonemes;
    }

    SingerCategory::SingerCategory()
        : ContribCategory(NAME, ModuleDeclaration, SingerProviderPlugin::IID) {
    }

    SingerCategory::~SingerCategory() = default;

    std::vector<SingerSpec *> SingerCategory::singers() const {
        std::vector<SingerSpec *> result;
        const auto values = contributions();
        result.reserve(values.size());
        for (auto value : values) {
            result.push_back(value->as<SingerSpec>());
        }
        return result;
    }

    Expected<std::unique_ptr<ContribSpec>>
        SingerCategory::createSpec(const ContribCreateContext &context) const {
        if (auto result = validateEntry(context.manifestEntry()); !result) {
            return result.takeError();
        }
        if (!context.manifestDeclaration() || !context.declarationPath()) {
            return Error(Error::InvalidFormat, "singer contribution requires a declaration");
        }
        // Unrecognized fields are retained and ignored, as the JSON profile of the specification
        // requires for every framework defined object. A newer package may contain fields that
        // this runtime does not support, and rejecting such a package would break forward
        // compatibility. Each unrecognized field is logged at debug level so that a misspelled
        // optional field can be diagnosed.
        const auto &declaration = *context.manifestDeclaration();
        static const std::set<std::string_view> known = {
            "avatar",  "background", "configuration",    "defaultLanguage", "demoAudio",
            "exports", "imports",    "interface",        "languages",       "level",
            "name",    "variant",    "reservedPhonemes",
        };
        for (const auto &item : declaration) {
            if (known.find(item.first) == known.end()) {
                logCategory().srtDebug("singer declaration field \"%1\" is not recognized by "
                                       "this runtime and is ignored",
                                       item.first);
            }
        }
        const auto base = context.declarationPath()->parent_path();
        const auto readOptionalPath = [&](std::string_view name,
                                          DisplayText *destination) -> Expected<void> {
            const auto it = declaration.find(name);
            if (it == declaration.end()) {
                return {};
            }
            auto result = readDisplayPath(it->second, name, base);
            if (!result) {
                return result.takeError();
            }
            *destination = result.take();
            return {};
        };
        DisplayText avatar;
        DisplayText background;
        DisplayText demoAudio;
        if (auto result = readOptionalPath("avatar", &avatar); !result) {
            return result.takeError();
        }
        if (auto result = readOptionalPath("background", &background); !result) {
            return result.takeError();
        }
        if (auto result = readOptionalPath("demoAudio", &demoAudio); !result) {
            return result.takeError();
        }
        std::map<std::string, std::string> languages;
        std::string defaultLanguage;
        if (auto result =
                readLanguages(declaration, context.imports(), &languages, &defaultLanguage);
            !result) {
            return result.takeError();
        }
        std::vector<std::string> reservedPhonemes;
        if (auto result = readReservedPhonemes(declaration, &reservedPhonemes); !result) {
            return result.takeError();
        }
        return std::unique_ptr<ContribSpec>(new SingerSpec(
            context, std::move(avatar), std::move(background), std::move(demoAudio),
            std::move(languages), std::move(defaultLanguage), std::move(reservedPhonemes)));
    }

}

static srt::ContribCategoryRegistry::Add<srt::SingerCategory>
    singerCategoryRegistration(srt::SingerCategory::NAME, "");
