#ifndef SYNTHRT_RUNTIMESERVICE_H
#define SYNTHRT_RUNTIMESERVICE_H

#include <cassert>
#include <string>
#include <utility>

#include <synthrt/synthrt_global.h>

namespace srt {

    class SynthUnit;

    /// A process resource or backend shared by executives in one SynthUnit.
    class RuntimeService {
    public:
        virtual ~RuntimeService() = default;

        /// Returns the stable identifier of the service interface.
        inline const std::string &iid() const {
            return m_iid;
        }

        /// Returns the implementation name within the service interface.
        inline const std::string &name() const {
            return m_name;
        }

        /// Returns the SynthUnit that owns this service.
        ///
        /// This function may only be called after successful registration.
        inline SynthUnit &synthUnit() const {
            assert(m_synthUnit);
            return *m_synthUnit;
        }

        SYNTHRT_DECLARE_AS_METHODS(RuntimeService)

    protected:
        inline RuntimeService(std::string iid, std::string name)
            : m_iid(std::move(iid)), m_name(std::move(name)) {
        }

        /// Moves an unregistered service.
        inline RuntimeService(RuntimeService &&RHS) noexcept
            : m_iid(std::move(RHS.m_iid)), m_name(std::move(RHS.m_name)) {
            assert(!RHS.m_synthUnit);
        }

        /// Moves an unregistered service into another unregistered service.
        inline RuntimeService &operator=(RuntimeService &&RHS) noexcept {
            assert(!m_synthUnit);
            assert(!RHS.m_synthUnit);
            m_iid = std::move(RHS.m_iid);
            m_name = std::move(RHS.m_name);
            return *this;
        }

    private:
        std::string m_iid;
        std::string m_name;

        // Set at registration
        SynthUnit *m_synthUnit = nullptr;

        STDC_DISABLE_COPY(RuntimeService)

        friend class SynthUnit;
    };

}

#endif // SYNTHRT_RUNTIMESERVICE_H
