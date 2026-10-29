#ifndef SYNTHRT_CORE_CONTRIBPLUGINFACTORY_P_H
#define SYNTHRT_CORE_CONTRIBPLUGINFACTORY_P_H

#include <map>
#include <memory>
#include <string_view>
#include <utility>

#include <stdcorelib/adt/array_view.h>
#include <stdcorelib/adt/vlarray.h>
#include <stdcorelib/plugin/pluginfactory.h>

#include <synthrt/Core/ContribInterpreter.h>
#include <synthrt/Core/ContribTriplet.h>
#include <synthrt/Support/Expected.h>

namespace srt {

    /// Discovers contribution plugins stored in one directory per plugin.
    class ContribPluginFactory : public stdc::plugin::BundlePluginFactory {
    public:
        /// Returns the first plugin whose valid metadata declares the requested interpreter.
        stdc::plugin::PluginLoader *findInterpreter(std::string_view iid,
                                                    const ContribTriplet &triplet) const;

        /// Loads the selected plugin and returns its retained interpreter for one contract.
        Expected<ContribInterpreter *> loadInterpreter(stdc::plugin::PluginLoader *loader,
                                                       const ContribTriplet &triplet);

        /// Returns the interpreters selected in this SynthUnit.
        stdc::array_view<ContribInterpreter *> interpreters() const;

        /// Returns import validators supplied by the interpreters selected in this SynthUnit.
        stdc::array_view<ContribImportValidator *> importValidators() const;

    private:
        using InterpreterKey = std::pair<stdc::plugin::PluginLoader *, ContribTriplet>;

        std::map<InterpreterKey, std::unique_ptr<ContribInterpreter>> m_interpreters;
        stdc::vlarray<ContribInterpreter *> m_interpreterViews;
        stdc::vlarray<std::unique_ptr<ContribImportValidator>> m_importValidatorData;
        stdc::vlarray<ContribImportValidator *> m_importValidators;
    };

}

#endif // SYNTHRT_CORE_CONTRIBPLUGINFACTORY_P_H
