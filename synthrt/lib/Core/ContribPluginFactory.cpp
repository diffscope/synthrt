#include "ContribPluginFactory_p.h"

#include <string>
#include <string_view>
#include <utility>

#include <stdcorelib/support/json.h>

#include "ContribInterpreterPlugin.h"

namespace srt {

    namespace {

        bool metadataProvidesInterpreter(const stdc::json::Value &metadata,
                                         const ContribContractTriplet &triplet) {
            // Metadata that breaks a rule makes the plugin no candidate at all, rather than a
            // candidate for the triplets it does spell correctly.
            const auto declarations =
                ContribInterpreterPlugin::readInterpreterDeclarations(metadata);
            if (!declarations) {
                return false;
            }
            for (const auto &declaration : declarations.value()) {
                if (declaration == triplet) {
                    return true;
                }
            }
            return false;
        }

    }

    stdc::plugin::PluginLoader *
        ContribPluginFactory::findInterpreter(std::string_view iid,
                                              const ContribContractTriplet &triplet) const {
        for (auto loader : plugins(iid)) {
            if (metadataProvidesInterpreter(loader->metadata(), triplet)) {
                return loader;
            }
        }
        return nullptr;
    }

    Expected<ContribInterpreter *>
        ContribPluginFactory::loadInterpreter(stdc::plugin::PluginLoader *loader,
                                              const ContribContractTriplet &triplet) {
        if (!loader) {
            return Error(Error::InvalidArgument, "interpreter plugin loader must not be null");
        }

        InterpreterKey key(loader, triplet);
        const auto cached = m_interpreters.find(key);
        if (cached != m_interpreters.end()) {
            return cached->second.get();
        }

        if (!loader->load()) {
            return Error(Error::FileNotOpen, "failed to load contribution interpreter plugin: " +
                                                 loader->errorMessage());
        }

        auto plugin = static_cast<ContribInterpreterPlugin *>(loader->plugin());
        auto result = plugin->create(triplet);
        if (!result) {
            return result.takeError().withContext("failed to create contribution interpreter");
        }
        auto interpreter = result.take();
        if (!interpreter) {
            return Error(Error::InvalidFormat,
                         "contribution interpreter plugin returned a null interpreter");
        }

        auto validators = interpreter->createImportValidators();
        if (!validators) {
            return validators.takeError().withContext(
                "contribution interpreter failed to create import validators");
        }
        auto preparedValidators = validators.take();
        for (const auto &validator : preparedValidators) {
            if (!validator) {
                return Error(Error::InvalidFormat,
                             "contribution interpreter returned a null import validator");
            }
        }

        for (auto &validator : preparedValidators) {
            m_importValidators.push_back(validator.get());
            m_importValidatorData.push_back(std::move(validator));
        }

        auto value = interpreter.get();
        m_interpreterViews.push_back(value);
        m_interpreters.emplace(std::move(key), std::move(interpreter));
        return value;
    }

    stdc::array_view<ContribInterpreter *> ContribPluginFactory::interpreters() const {
        return m_interpreterViews;
    }

    stdc::array_view<ContribImportValidator *> ContribPluginFactory::importValidators() const {
        return m_importValidators;
    }

}
