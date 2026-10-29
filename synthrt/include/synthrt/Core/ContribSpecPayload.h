#ifndef SYNTHRT_CORE_CONTRIBSPECPAYLOAD_H
#define SYNTHRT_CORE_CONTRIBSPECPAYLOAD_H

#include <utility>

#include <synthrt/Core/ContribTriplet.h>
#include <synthrt/synthrt_global.h>

namespace srt {

    /// A typed memory representation of one manifest value specific to a contract.
    class ContribSpecPayload {
    public:
        virtual ~ContribSpecPayload() = default;

        /// Returns the triplet of the contract this value belongs to.
        const ContribTriplet &triplet() const {
            return m_triplet;
        }

        SYNTHRT_DECLARE_AS_METHODS(ContribSpecPayload)

    protected:
        explicit ContribSpecPayload(ContribTriplet triplet) : m_triplet(std::move(triplet)) {
        }

    private:
        ContribTriplet m_triplet;

        STDC_DISABLE_COPY(ContribSpecPayload)
    };

}

#endif // SYNTHRT_CORE_CONTRIBSPECPAYLOAD_H
