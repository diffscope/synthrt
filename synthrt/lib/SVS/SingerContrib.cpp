#include "SingerContrib.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <stdcorelib/path.h>

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

        Expected<void> validateDeclaration(const JsonObject &declaration) {
            static const std::set<std::string_view> fields = {
                "avatar",  "background", "configuration", "demoAudio", "exports",
                "imports", "interface",  "level",         "name",      "variant",
            };
            for (const auto &item : declaration) {
                if (fields.find(item.first) == fields.end()) {
                    return Error(Error::InvalidFormat, "singer declaration has an unknown field");
                }
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
                           DisplayText background, DisplayText demoAudio)
        : ContribSpec(context), m_avatar(std::move(avatar)), m_background(std::move(background)),
          m_demoAudio(std::move(demoAudio)) {
        m_declarationPath = *context.declarationPath();
    }

    SingerSpec::~SingerSpec() = default;

    const fs::path &SingerSpec::declarationPath() const {
        return m_declarationPath;
    }

    const DisplayText &SingerSpec::avatar() const {
        return m_avatar;
    }

    const DisplayText &SingerSpec::background() const {
        return m_background;
    }

    const DisplayText &SingerSpec::demoAudio() const {
        return m_demoAudio;
    }

    SingerCategory::SingerCategory()
        : ContribCategory("singer", ModuleDeclaration, SingerProviderPlugin::IID) {
    }

    SingerCategory::~SingerCategory() = default;

    std::vector<SingerSpec *> SingerCategory::singers() const {
        std::vector<SingerSpec *> result;
        const auto values = contributions();
        result.reserve(values.size());
        for (auto *value : values) {
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
        if (auto result = validateDeclaration(*context.manifestDeclaration()); !result) {
            return result.takeError();
        }

        const auto &declaration = *context.manifestDeclaration();
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
        return std::unique_ptr<ContribSpec>(new SingerSpec(
            context, std::move(avatar), std::move(background), std::move(demoAudio)));
    }

}

static srt::ContribCategoryRegistry::Add<srt::SingerCategory> singerCategoryRegistration("singer",
                                                                                         "");
