#ifndef DSINFER_INFERENCEDRIVER_H
#define DSINFER_INFERENCEDRIVER_H

#include <synthrt/Support/JSON.h>
#include <synthrt/Support/Expected.h>
#include <synthrt/Core/NamedObject.h>

namespace ds {

    class InferenceDriverInitArgs : public srt::NamedObject {
    public:
        inline InferenceDriverInitArgs(std::string name, int version)
            : srt::NamedObject(std::move(name)), version(version) {
        }

        int version;
    };

    /// Extras a driver offers beyond this interface, for a caller that knows which backend it is
    /// talking to.
    ///
    /// Told apart by objectName(), the way the argument and result types are.
    class InferenceDriverExtension : public srt::NamedObject {
    public:
        inline InferenceDriverExtension(std::string name, int version)
            : srt::NamedObject(std::move(name)), version(version) {
        }

        int version;
    };

    class InferenceSession;

    /// InferenceDriver - DiffSinger inference driver interface.
    ///
    /// \note An instance of \c InferenceDriver needs to be added to the \c InferenceCategory with
    /// the ID "dsdriver" before it can be called by the inference interpreters.
    ///
    /// It is used like the following.
    /// \code
    ///     void init(srt::SynthUnit &su, srt::UNO<InferenceDriver> driver) {
    ///         ContribCategory &ic = *su.category("inference");
    ///         ic.addUniqueObject("dsdriver", std::move(driver));
    ///     }
    /// \endcode
    ///
    /// The category is the owner, and an interpreter borrows the driver for as long as it runs.
    class InferenceDriver : public srt::NamedObject {
    public:
        virtual ~InferenceDriver() = default;

        /// Related singer arch.
        virtual std::string arch() const = 0;

        /// Driver backend identifier.
        virtual std::string backend() const = 0;

        virtual srt::Expected<void> initialize(const srt::NO<InferenceDriverInitArgs> &args) = 0;

        virtual srt::UNO<InferenceSession> createSession() = 0;

        /// What this backend offers on top of the interface, or null when it offers nothing or
        /// initialize() has not succeeded yet.
        ///
        /// \warning Borrowed, and only for as long as this driver lives. What it points into
        ///          belongs to the backend's runtime, and the driver is what keeps that loaded.
        ///
        /// \code
        ///     if (auto ext = driver->extension();
        ///         ext && ext->objectName() == Api::Onnx::API_NAME) {
        ///         auto onnx = static_cast<const Api::Onnx::DriverExtension *>(ext);
        ///         Ort::InitApi(onnx->ortApi);
        ///     }
        /// \endcode
        virtual const InferenceDriverExtension *extension() const {
            return nullptr;
        }
    };

}

#endif // DSINFER_INFERENCEDRIVER_H
