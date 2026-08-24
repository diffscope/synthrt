#include "Logging.h"

namespace srt {

    LogCategory &logCategory() {
        static LogCategory category("synthrt");
        return category;
    }

}
