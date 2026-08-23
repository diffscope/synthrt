#ifndef SYNTHRT_CONTRIBPLUGINFACTORY_P_H
#define SYNTHRT_CONTRIBPLUGINFACTORY_P_H

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <stdcorelib/plugin/pluginfactory.h>

#include <synthrt/Core/ContribInterpreter.h>
#include <synthrt/Support/Expected.h>

namespace srt {

    /// Discovers contribution plugins stored in one directory per plugin.
    class ContribPluginFactory final : public stdc::plugin::PluginFactory {
    public:
        /// Returns the first plugin whose valid metadata declares the requested interpreter.
        stdc::plugin::PluginLoader *findInterpreter(std::string_view iid,
                                                    std::string_view interfaceName,
                                                    std::string_view variant, int level) const;

        /// Loads the selected plugin once and returns its retained interpreter.
        Expected<ContribInterpreter *> loadInterpreter(stdc::plugin::PluginLoader *loader);

    protected:
        bool scanPluginPaths(const std::filesystem::path &path,
                             std::vector<std::filesystem::path> *pluginPaths) const override;

        bool resolvePluginPath(const std::filesystem::path &path, std::filesystem::path *pluginPath,
                               std::optional<std::filesystem::path> *manifestPath) const override;

    private:
        std::map<stdc::plugin::PluginLoader *, std::unique_ptr<ContribInterpreter>> m_interpreters;
    };

}

#endif // SYNTHRT_CONTRIBPLUGINFACTORY_P_H
