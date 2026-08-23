#ifndef SYNTHRT_CONTRIBITEM_H
#define SYNTHRT_CONTRIBITEM_H

#include <synthrt/synthrt_global.h>

namespace srt {

    /// A typed interpretation of one contribution's manifest \c exports value.
    class SYNTHRT_EXPORT ContribExportItem {
    public:
        virtual ~ContribExportItem();

        SYNTHRT_DECLARE_AS_METHODS(ContribExportItem)

    protected:
        ContribExportItem() = default;
    };

    /// A typed interpretation of one import entry's manifest \c options value.
    class SYNTHRT_EXPORT ContribImportItem {
    public:
        virtual ~ContribImportItem();

        SYNTHRT_DECLARE_AS_METHODS(ContribImportItem)

    protected:
        ContribImportItem() = default;
    };

    /// A typed interpretation of one contribution's manifest \c configuration value.
    class SYNTHRT_EXPORT ContribConfiguration {
    public:
        virtual ~ContribConfiguration();

        SYNTHRT_DECLARE_AS_METHODS(ContribConfiguration)

    protected:
        ContribConfiguration() = default;
    };

}

#endif // SYNTHRT_CONTRIBITEM_H
