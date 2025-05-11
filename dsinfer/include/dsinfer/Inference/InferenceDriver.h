#ifndef DSINFER_INFERENCEDRIVER_H
#define DSINFER_INFERENCEDRIVER_H

#include <synthrt/Support/JSON.h>
#include <synthrt/Support/Error.h>
#include <synthrt/Core/NamedObject.h>

namespace ds {

    class InferenceDriverInitArgs : public srt::NamedObject {
    public:
        inline InferenceDriverInitArgs(const std::string &name, int version)
            : srt::NamedObject(name), version(version) {
        }

        int version;
    };

    class InferenceSession;

    /// \note An instance of \a InferenceDriver needs to be added to the \a InferenceCategory with
    /// the ID "dsdriver" before it can be called by the inference interpreters.
    class InferenceDriver : public srt::NamedObject {
    public:
        virtual ~InferenceDriver() = default;

        virtual bool initialize(const srt::NO<InferenceDriverInitArgs> &args,
                                srt::Error *error) = 0;

        virtual srt::NO<InferenceSession> createSession() = 0;
    };

}

#endif // DSINFER_INFERENCEDRIVER_H