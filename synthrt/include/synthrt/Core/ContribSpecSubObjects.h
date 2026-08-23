#ifndef SYNTHRT_CONTRIBSPECSUBOBJECTS_H
#define SYNTHRT_CONTRIBSPECSUBOBJECTS_H

#include <synthrt/synthrt_global.h>

namespace srt {

    /// A typed interpretation of one contribution's manifest \c exports value.
    class ContribExports {
    public:
        virtual ~ContribExports() = default;

        SYNTHRT_DECLARE_AS_METHODS(ContribExports)

    protected:
        ContribExports() = default;
    };

    /// A typed interpretation of one import entry's manifest \c options value.
    class ContribImportOptions {
    public:
        virtual ~ContribImportOptions() = default;

        SYNTHRT_DECLARE_AS_METHODS(ContribImportOptions)

    protected:
        ContribImportOptions() = default;
    };

    /// A typed interpretation of one contribution's manifest \c configuration value.
    class ContribConfiguration {
    public:
        virtual ~ContribConfiguration() = default;

        SYNTHRT_DECLARE_AS_METHODS(ContribConfiguration)

    protected:
        ContribConfiguration() = default;
    };

}

#endif // SYNTHRT_CONTRIBSPECSUBOBJECTS_H
