#ifndef SYNTHRT_CONTRIBPLUGINFACTORY_P_H
#define SYNTHRT_CONTRIBPLUGINFACTORY_P_H

#include <filesystem>
#include <optional>
#include <vector>

#include <stdcorelib/plugin/pluginfactory.h>

namespace srt {

    /// Discovers contribution plugins stored in one directory per plugin.
    class ContribPluginFactory final : public stdc::plugin::PluginFactory {
    protected:
        bool scanPluginPaths(const std::filesystem::path &path,
                             std::vector<std::filesystem::path> *pluginPaths) const override;

        bool resolvePluginPath(const std::filesystem::path &path, std::filesystem::path *pluginPath,
                               std::optional<std::filesystem::path> *manifestPath) const override;
    };

}

#endif // SYNTHRT_CONTRIBPLUGINFACTORY_P_H
