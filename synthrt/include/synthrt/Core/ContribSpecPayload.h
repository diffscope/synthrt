#ifndef SYNTHRT_CORE_CONTRIBSPECPAYLOAD_H
#define SYNTHRT_CORE_CONTRIBSPECPAYLOAD_H

#include <synthrt/Core/ContribContract.h>
#include <synthrt/synthrt_global.h>

namespace srt {

    /// A typed memory representation of one manifest value specific to a contract.
    class ContribSpecPayload {
    public:
        virtual ~ContribSpecPayload() = default;

        /// Returns the interface contract this value belongs to.
        virtual const ContribContract &contract() const = 0;

        SYNTHRT_DECLARE_AS_METHODS(ContribSpecPayload)

    protected:
        ContribSpecPayload() = default;

    private:
        STDC_DISABLE_COPY(ContribSpecPayload)
    };

}

#endif // SYNTHRT_CORE_CONTRIBSPECPAYLOAD_H
