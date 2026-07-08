#include "ServiceRegistry.h"

namespace srt::core {

    ServiceRegistry::ServiceRegistry() = default;

    ServiceRegistry::~ServiceRegistry() {
        for (auto &[_, entry] : _services) {
            if (entry.deleter) {
                entry.deleter(entry.ptr);
            }
        }
    }

}
