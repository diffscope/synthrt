#ifndef SYNTHRT_SINGERPROVIDER_H
#define SYNTHRT_SINGERPROVIDER_H

#include <synthrt/Core/ContribInterpreter.h>

namespace srt {

    /// Interprets and executes singer contributions.
    class SingerProvider : public ContribInterpreter {
    public:
        ~SingerProvider() override = default;

    protected:
        SingerProvider() = default;
    };

}

#endif // SYNTHRT_SINGERPROVIDER_H
