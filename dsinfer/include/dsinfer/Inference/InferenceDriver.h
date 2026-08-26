#ifndef DSINFER_INFERENCEDRIVER_H
#define DSINFER_INFERENCEDRIVER_H

#include <memory>
#include <string>
#include <utility>

#include <synthrt/Core/RuntimeService.h>
#include <synthrt/Support/Expected.h>

#include <dsinfer/dsinfer_global.h>

namespace ds {

    /// A typed value exchanged with an inference driver.
    class InferenceDriverPayload {
    public:
        virtual ~InferenceDriverPayload() = default;

        /// Returns the backend contract understood by this value.
        const std::string &type() const noexcept {
            return m_type;
        }

        /// Returns the version of the backend contract.
        int version() const noexcept {
            return m_version;
        }

        SYNTHRT_DECLARE_AS_METHODS(InferenceDriverPayload)

    protected:
        InferenceDriverPayload(std::string type, int version)
            : m_type(std::move(type)), m_version(version) {
        }

    private:
        std::string m_type;
        int m_version;

        STDC_DISABLE_COPY(InferenceDriverPayload)
    };

    /// Initialization data supplied to an inference driver.
    class InferenceDriverInitArgs : public InferenceDriverPayload {
    public:
        virtual ~InferenceDriverInitArgs() = default;

    protected:
        using InferenceDriverPayload::InferenceDriverPayload;
    };

    /// Backend specific state exposed to callers that understand its contract.
    class InferenceDriverExtension : public InferenceDriverPayload {
    public:
        virtual ~InferenceDriverExtension() = default;

    protected:
        using InferenceDriverPayload::InferenceDriverPayload;
    };

    class InferenceSession;

    /// A process backend used to execute inference models.
    class DSINFER_EXPORT InferenceDriver : public srt::RuntimeService {
    public:
        virtual ~InferenceDriver();

        /// Returns the backend contract name.
        const std::string &backend() const noexcept {
            return name();
        }

        /// Initializes process resources used by sessions from this driver.
        virtual srt::Expected<void> initialize(const InferenceDriverInitArgs &args) = 0;

        /// Creates an unopened session.
        virtual std::unique_ptr<InferenceSession> createSession() = 0;

        /// Returns optional backend specific state after successful initialization.
        virtual const InferenceDriverExtension *extension() const {
            return nullptr;
        }

        SYNTHRT_DECLARE_AS_METHODS(InferenceDriver)

    protected:
        explicit InferenceDriver(std::string backend);
    };

}

#endif // DSINFER_INFERENCEDRIVER_H
