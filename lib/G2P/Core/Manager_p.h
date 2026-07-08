#ifndef SRT_G2P_CORE_MANAGER_P_H
#define SRT_G2P_CORE_MANAGER_P_H

#include <synthrt/G2P/Core/Manager.h>

#include "Core/PackageManager_p.h"

namespace srt::g2p {

    class Manager::Impl : public PackageManager::Impl {
    public:
        explicit Impl(Manager *decl);
        ~Impl() override;

        using Decl = Manager;
    };

} // namespace srt::g2p

#endif // SRT_G2P_CORE_MANAGER_P_H
