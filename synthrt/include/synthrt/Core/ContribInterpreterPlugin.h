#ifndef SYNTHRT_CORE_CONTRIBINTERPRETERPLUGIN_H
#define SYNTHRT_CORE_CONTRIBINTERPRETERPLUGIN_H

#include <memory>

#include <stdcorelib/plugin/plugin.h>

#include <synthrt/Core/ContribInterpreter.h>
#include <synthrt/Core/ContribTriplet.h>
#include <synthrt/Support/Expected.h>

namespace srt {

    /// A plugin factory that creates contribution interpreters.
    class ContribInterpreterPlugin : public stdc::plugin::Plugin {
    public:
        ~ContribInterpreterPlugin() = default;

        /// Creates the interpreter for  triplet, one of the triplets that the metadata of the
        /// plugin declares.
        virtual Expected<std::unique_ptr<ContribInterpreter>>
            create(const ContribTriplet &triplet) = 0;

    protected:
        ContribInterpreterPlugin() = default;
    };

}

#endif // SYNTHRT_CORE_CONTRIBINTERPRETERPLUGIN_H
