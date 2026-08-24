#ifndef SYNTHRT_CONTRIBSPECPAYLOAD_H
#define SYNTHRT_CONTRIBSPECPAYLOAD_H

#include <string>
#include <utility>

#include <synthrt/synthrt_global.h>

namespace srt {

    /// A typed memory representation of one manifest value specific to a contract.
    class ContribSpecPayload {
    public:
        virtual ~ContribSpecPayload() = default;

        inline const std::string &interface() const {
            return m_interface;
        }

        inline const std::string &variant() const {
            return m_variant;
        }

        inline int level() const noexcept {
            return m_level;
        }

        SYNTHRT_DECLARE_AS_METHODS(ContribSpecPayload)

    protected:
        ContribSpecPayload(std::string interface, std::string variant, int level)
            : m_interface(std::move(interface)), m_variant(std::move(variant)), m_level(level) {
        }

    private:
        std::string m_interface;
        std::string m_variant;
        int m_level;

        STDC_DISABLE_COPY_MOVE(ContribSpecPayload)
    };

}

#endif // SYNTHRT_CONTRIBSPECPAYLOAD_H
