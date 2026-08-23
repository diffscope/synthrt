#ifndef SYNTHRT_CONTRIBINTERPRETERPLUGIN_H
#define SYNTHRT_CONTRIBINTERPRETERPLUGIN_H

#include <memory>

#include <stdcorelib/plugin/plugin.h>

#include <synthrt/Core/ContribInterpreter.h>
#include <synthrt/Support/Expected.h>
#include <synthrt/synthrt_global.h>

namespace srt {

    /// A plugin factory that creates contribution interpreters.
    class SYNTHRT_EXPORT ContribInterpreterPlugin : public stdc::plugin::Plugin {
    public:
        ~ContribInterpreterPlugin() override;

        /// Creates an interpreter implemented by this plugin.
        virtual Expected<std::unique_ptr<ContribInterpreter>> create() = 0;

    protected:
        ContribInterpreterPlugin() = default;
    };

}

#endif // SYNTHRT_CONTRIBINTERPRETERPLUGIN_H
