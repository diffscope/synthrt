#ifndef SYNTHRT_CORE_CONTRIBINTERPRETERPLUGIN_H
#define SYNTHRT_CORE_CONTRIBINTERPRETERPLUGIN_H

#include <memory>
#include <vector>

#include <stdcorelib/plugin/plugin.h>
#include <stdcorelib/support/json.h>

#include <synthrt/Core/ContribInterpreter.h>
#include <synthrt/Core/ContribTriplet.h>
#include <synthrt/Support/Expected.h>

namespace srt {

    /// A plugin factory that creates contribution interpreters.
    class ContribInterpreterPlugin : public stdc::plugin::Plugin {
    public:
        ~ContribInterpreterPlugin() = default;

        /// Creates the interpreter for \a triplet, one of the triplets that the metadata of the
        /// plugin declares.
        ///
        /// The runtime passes only declared triplets. A plugin refuses any other with an error of
        /// its own choosing, since the specification names no code for that refusal.
        virtual Expected<std::unique_ptr<ContribInterpreter>>
            create(const ContribTriplet &triplet) = 0;

        /// Reads the triplets that the metadata of an interpreter plugin declares.
        ///
        /// The metadata is the \c plugin.json of the bundle. Its \c name is a valid segment, and
        /// its \c interpreters array is nonempty and holds distinct objects, each with an
        /// \c interface and a \c variant that are valid dotted identifiers and a positive integer
        /// \c level.
        ///
        /// Provider discovery reads the metadata with this function and passes over a plugin whose
        /// metadata it rejects, without loading it. The error therefore explains why a plugin is
        /// never selected, which discovery itself does not report.
        ///
        /// \return The declarations in the order of the array, or an \c InvalidFormat error that
        ///         names the first rule the metadata breaks.
        SYNTHRT_EXPORT static Expected<std::vector<ContribTriplet>>
            readInterpreterDeclarations(const stdc::json::Value &metadata);

    protected:
        ContribInterpreterPlugin() = default;
    };

}

#endif // SYNTHRT_CORE_CONTRIBINTERPRETERPLUGIN_H
