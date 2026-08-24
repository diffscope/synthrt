#ifndef SYNTHRT_CONTRIBPLUGINFACTORY_P_H
#define SYNTHRT_CONTRIBPLUGINFACTORY_P_H

#include <map>
#include <memory>
#include <string_view>

#include <stdcorelib/plugin/pluginfactory.h>

#include <synthrt/Core/ContribInterpreter.h>
#include <synthrt/Support/Expected.h>

namespace srt {

    /// Discovers contribution plugins stored in one directory per plugin.
    class ContribPluginFactory : public stdc::plugin::BundlePluginFactory {
    public:
        /// Returns the first plugin whose valid metadata declares the requested interpreter.
        stdc::plugin::PluginLoader *findInterpreter(std::string_view iid,
                                                    std::string_view interfaceName,
                                                    std::string_view variant, int level) const;

        /// Loads the selected plugin once and returns its retained interpreter.
        Expected<ContribInterpreter *> loadInterpreter(stdc::plugin::PluginLoader *loader);

    private:
        std::map<stdc::plugin::PluginLoader *, std::unique_ptr<ContribInterpreter>> m_interpreters;
    };

}

#endif // SYNTHRT_CONTRIBPLUGINFACTORY_P_H
