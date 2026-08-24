#ifndef SYNTHRT_CONTRIBPLUGINFACTORY_P_H
#define SYNTHRT_CONTRIBPLUGINFACTORY_P_H

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>

#include <stdcorelib/plugin/pluginfactory.h>

#include <synthrt/Core/ContribInterpreter.h>
#include <synthrt/Support/Expected.h>

namespace srt {

    /// Discovers contribution plugins stored in one directory per plugin.
    class ContribPluginFactory : public stdc::plugin::BundlePluginFactory {
    public:
        /// Returns the first plugin whose valid metadata declares the requested interpreter.
        stdc::plugin::PluginLoader *findInterpreter(std::string_view iid,
                                                    std::string_view interfaceName, int level,
                                                    std::string_view variant) const;

        /// Loads the selected plugin and returns its retained interpreter for one contract.
        Expected<ContribInterpreter *> loadInterpreter(stdc::plugin::PluginLoader *loader,
                                                       std::string_view interfaceName, int level,
                                                       std::string_view variant);

    private:
        using InterpreterKey =
            std::tuple<stdc::plugin::PluginLoader *, std::string, int, std::string>;

        std::map<InterpreterKey, std::unique_ptr<ContribInterpreter>> m_interpreters;
    };

}

#endif // SYNTHRT_CONTRIBPLUGINFACTORY_P_H
