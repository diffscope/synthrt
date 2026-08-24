#ifndef DSINFER_ONNXDRIVER_SESSIONIMAGE_H
#define DSINFER_ONNXDRIVER_SESSIONIMAGE_H

#include <filesystem>

#include <onnxruntime_cxx_api.h>

#include <stdcorelib/adt/vlarray.h>

#include <synthrt/Support/Expected.h>

namespace ds::onnxdriver {

    class DriverContext;

    /// Owns an initialized ORT model session and its input and output names.
    class SessionImage {
    public:
        SessionImage();
        ~SessionImage();

        /// Opens \a onnxPath using the runtime and execution settings in \a context.
        srt::Expected<void> open(DriverContext &context, const std::filesystem::path &onnxPath,
                                 bool preferCpu);

        /// Returns the input names declared by the model.
        const stdc::vlarray_base<std::string> &inputNames() const noexcept;

        /// Returns the output names declared by the model.
        const stdc::vlarray_base<std::string> &outputNames() const noexcept;

        /// Returns the initialized ORT session.
        Ort::Session &session() noexcept;

    private:
        stdc::vlarray<std::string, 8> m_inputNames;
        stdc::vlarray<std::string, 8> m_outputNames;
        Ort::Session m_session;
    };

}

#endif // DSINFER_ONNXDRIVER_SESSIONIMAGE_H
