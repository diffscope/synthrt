#pragma once

#include <memory>
#include <typeindex>
#include <unordered_map>

#include <synthrt/Core/srt_core_global.h>

namespace srt::core {

    /// ServiceRegistry - Type-indexed service locator owned by Runtime.
    ///
    /// Implements ARCH-03 (composition over inheritance): Runtime owns a
    /// ServiceRegistry and registers its services (PluginService, etc.) here.
    /// Callers query services by type without Runtime inheriting any service
    /// base class. The registry owns the registered service instances.
    class SRT_CORE_EXPORT ServiceRegistry {
    public:
        ServiceRegistry();
        ~ServiceRegistry();

        ServiceRegistry(const ServiceRegistry &) = delete;
        ServiceRegistry &operator=(const ServiceRegistry &) = delete;

        /// Register a service instance, transferring ownership. Returns a
        /// non-owning pointer to the registered service. Re-registering a type
        /// destroys the previously registered instance.
        template <class T>
        T *registerService(std::unique_ptr<T> service);

        /// Look up a service by type. Returns nullptr if no service of type T
        /// has been registered.
        template <class T>
        T *get() const;

    private:
        struct Entry {
            void *ptr = nullptr;
            void (*deleter)(void *) = nullptr;
        };
        std::unordered_map<std::type_index, Entry> _services;
    };

    // ---- template implementations ----

    template <class T>
    inline T *ServiceRegistry::registerService(std::unique_ptr<T> service) {
        static_assert(!std::is_reference_v<T>, "T must not be a reference");
        auto *raw = service.get();
        auto &slot = _services[std::type_index(typeid(T))];
        if (slot.deleter) {
            slot.deleter(slot.ptr);
        }
        slot.ptr = raw;
        slot.deleter = [](void *p) { delete static_cast<T *>(p); };
        service.release();
        return raw;
    }

    template <class T>
    inline T *ServiceRegistry::get() const {
        auto it = _services.find(std::type_index(typeid(T)));
        if (it == _services.end() || it->second.ptr == nullptr) {
            return nullptr;
        }
        return static_cast<T *>(it->second.ptr);
    }

}
