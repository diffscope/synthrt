#ifndef DSINFER_INFERENCE_INFERENCEDRIVER_H
#define DSINFER_INFERENCE_INFERENCEDRIVER_H

#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include <synthrt/Core/ContribContract.h>
#include <synthrt/Core/RuntimeService.h>
#include <synthrt/Support/Expected.h>

#include <dsinfer/dsinfer_global.h>

namespace ds {

    /// An owning identity for an inference backend and the level of the API it speaks.
    ///
    /// The level is the backend's own API level. It is independent of the level of any inference
    /// contract whose payloads travel through the driver, and the two numbers are never compared.
    class InferenceBackendContract {
    public:
        InferenceBackendContract() = default;

        InferenceBackendContract(std::string name, int level)
            : m_name(std::move(name)), m_level(level) {
        }

        /// Returns the backend name that plugin discovery and diagnostics use.
        const std::string &name() const noexcept {
            return m_name;
        }

        /// Returns the level of the backend API.
        int level() const noexcept {
            return m_level;
        }

        /// Returns this identity in the form that the Task protocol carries.
        ///
        /// A backend states a name where a contribution states an interface identifier, and both
        /// state the level of the API they speak.
        operator srt::ContribContract() const {
            return srt::ContribContract(m_name, m_level);
        }

        /// Renders the identity for a diagnostic message.
        std::string toString() const {
            return m_name + ", level " + std::to_string(m_level);
        }

        bool operator==(const InferenceBackendContract &other) const {
            return m_name == other.m_name && m_level == other.m_level;
        }

        bool operator!=(const InferenceBackendContract &other) const {
            return !(*this == other);
        }

        /// Orders contracts by name and then level.
        bool operator<(const InferenceBackendContract &other) const {
            return std::tie(m_name, m_level) < std::tie(other.m_name, other.m_level);
        }

    private:
        std::string m_name;
        int m_level = 0;
    };

    /// A typed value exchanged with an inference driver.
    class InferenceDriverPayload {
    public:
        virtual ~InferenceDriverPayload() = default;

        /// Returns the backend contract understood by this value.
        const InferenceBackendContract &contract() const noexcept {
            return m_contract;
        }

        SYNTHRT_DECLARE_AS_METHODS(InferenceDriverPayload)

    protected:
        explicit InferenceDriverPayload(InferenceBackendContract contract)
            : m_contract(std::move(contract)) {
        }

    private:
        InferenceBackendContract m_contract;

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

#endif // DSINFER_INFERENCE_INFERENCEDRIVER_H
