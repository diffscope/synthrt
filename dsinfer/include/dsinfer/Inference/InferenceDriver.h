#ifndef SYNTHRT_INFERENCEDRIVER_H
#define SYNTHRT_INFERENCEDRIVER_H

#include <synthrt/Support/JSON.h>
#include <synthrt/Support/Error.h>
#include <synthrt/Core/Object.h>

namespace ds {

    class InferenceDriverInitArgs : public srt::NamedObject {
    public:
        InferenceDriverInitArgs(const std::string &name) : srt::NamedObject(name) {
        }
    };

    class InferenceSession;

    class InferenceDriver : public srt::Object {
    public:
        virtual ~InferenceDriver() = default;

        virtual bool initialize(const srt::NO<InferenceDriverInitArgs> &args,
                                srt::Error *error) = 0;

        virtual InferenceSession *createSession() = 0;
    };

}

#endif // SYNTHRT_INFERENCEDRIVER_H