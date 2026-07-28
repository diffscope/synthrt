// G2pOnnxSetup.cpp - Setup G2P ONNX driver by reusing Runtime's inference ONNX driver.
//
// Extracted from ds-editor-lite SynthrtEngine.cpp (G2pOnnxSessionTask /
// G2pOnnxSessionFactory file-local adapters + initializeG2pOnnxDriver) as
// task A1. This is the G2P-side counterpart of srt::driver::setupOnnxInferenceDriver:
// it reuses the Runtime's "inference/dsdriver" InferenceDriver, wraps it with
// a CPU-only SessionFactory adapter (forces useCpu=true so G2P never competes
// with GPU-bound SVS inference), and registers the adapter in the process-
// level srt::g2p::Manager's kDriverCategory under kG2pOnnxDriverName.

#include <synthrt/G2P/G2pOnnxSetup.h>

#include <filesystem>
#include <string>
#include <utility>

#include <synthrt/Core/Core/NamedObject.h>
#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/Core/Support/Error.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Driver/InferenceDriver.h>
#include <synthrt/Driver/InferenceSession.h>
#include <synthrt/Driver/onnx/OnnxDriverApi.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Core/Manager.h>
#include <synthrt/G2P/Task/SessionFactory.h>
#include <synthrt/G2P/Task/SessionTask.h>

namespace srt::g2p {

    namespace {

        /// G2pOnnxSessionTask - adapts srt::driver::InferenceSession to
        /// srt::g2p::SessionTask, forcing useCpu=true on every open() so G2P
        /// inference never competes with GPU-bound SVS inference.
        class G2pOnnxSessionTask : public srt::g2p::SessionTask {
        public:
            explicit G2pOnnxSessionTask(srt::core::NO<srt::driver::InferenceSession> session)
                : m_session(std::move(session)) {}

            int apiLevel() const override {
                return 1;
            }

            srt::core::Expected<void> initialize() override {
                // The underlying InferenceSession is already initialized when
                // the InferenceDriver created it; nothing to do here.
                return {};
            }

            srt::core::Expected<srt::core::NO<srt::core::TaskResult>> start(
                const srt::core::NO<srt::core::TaskStartInput> &input) override {
                // Translate G2P SessionStartInput → ONNX SessionStartInput.
                auto g2pInput = input.as<srt::g2p::SessionStartInput>();
                auto onnxInput =
                    srt::core::NO<srt::driver::onnx::SessionStartInput>::create();
                onnxInput->inputs = g2pInput->inputs;
                onnxInput->outputs = g2pInput->outputs;

                auto exp = m_session->start(onnxInput);
                if (!exp) {
                    return exp.takeError();
                }
                auto taskResult = exp.take();
                if (!taskResult) {
                    return srt::core::Error(
                        srt::core::ErrorCode::InferenceRunFailed,
                        "G2pOnnxSessionTask: inference session returned null result");
                }
                // Convert ONNX SessionResult → G2P SessionResult. The two have
                // different inheritance chains (onnx::SessionResult extends
                // InferenceSessionResult; g2p::SessionResult extends TaskResult
                // directly), so a static_cast across them would be UB — copy
                // the outputs map explicitly.
                auto onnxResult = taskResult.as<srt::driver::onnx::SessionResult>();
                auto g2pResult = srt::core::NO<srt::g2p::SessionResult>::create();
                g2pResult->outputs = onnxResult->outputs;
                return g2pResult;
            }

            srt::core::Expected<void> open(const std::filesystem::path &path,
                                           const srt::core::NO<srt::core::TaskInitArgs> &args) override {
                // Force useCpu=true (highest priority per onnx::SessionOpenArgs):
                // G2P must not compete with GPU inference. The caller-supplied
                // args (g2p::SessionOpenArgs::useCpu) is intentionally ignored.
                (void) args;
                auto openArgs =
                    srt::core::NO<srt::driver::onnx::SessionOpenArgs>::create();
                openArgs->useCpu = true;
                return m_session->open(path, openArgs);
            }

            srt::core::Expected<void> close() override {
                return m_session->close();
            }

            bool isOpen() const override {
                return m_session->isOpen();
            }

            int64_t id() const override {
                return m_session->id();
            }

        private:
            srt::core::NO<srt::driver::InferenceSession> m_session;
        };

        /// G2pOnnxSessionFactory - adapts srt::driver::InferenceDriver to
        /// srt::g2p::SessionFactory. Holds a weak_ptr to the driver: the
        /// driver's lifetime is owned by the Runtime's ObjectPool
        /// ("inference/dsdriver"). Using weak_ptr (instead of shared_ptr)
        /// avoids a crash at process exit: G2P Manager is a static singleton
        /// whose destructor runs AFTER the Runtime (a local variable in the
        /// host). If the factory held a shared_ptr, the driver's virtual
        /// destructor would run after the plugin DLL (srt-driver-onnx.dll)
        /// is unloaded by PluginFactory, accessing freed memory. With
        /// weak_ptr, the factory's destructor does not trigger driver
        /// destruction; the driver is destroyed by the Runtime while the
        /// plugin DLL is still loaded.
        class G2pOnnxSessionFactory : public srt::g2p::SessionFactory {
        public:
            explicit G2pOnnxSessionFactory(srt::core::NO<srt::driver::InferenceDriver> driver)
                : m_driver(std::move(driver)) {}

            std::string arch() const override {
                if (auto sp = m_driver.lock())
                    return sp->arch();
                return {};
            }

            std::string backend() const override {
                if (auto sp = m_driver.lock())
                    return sp->backend();
                return {};
            }

            srt::core::Expected<void> initialize(
                const srt::core::NO<srt::core::TaskInitArgs> &args) override {
                // The underlying InferenceDriver is already initialized by
                // srt::driver::setupOnnxInferenceDriver; no re-initialization.
                (void) args;
                return {};
            }

            srt::core::NO<srt::g2p::SessionTask> createSession() override {
                auto sp = m_driver.lock();
                if (!sp)
                    return nullptr;
                auto session = sp->createSession();
                if (!session) {
                    return nullptr;
                }
                return srt::core::NO<G2pOnnxSessionTask>::create(std::move(session));
            }

        private:
            std::weak_ptr<srt::driver::InferenceDriver> m_driver;
        };

    } // namespace

    srt::core::Expected<void> setupG2pOnnxDriver(
        srt::core::Runtime &runtime,
        const std::vector<std::filesystem::path> &g2pPluginPaths) {

        try {
            // 1. Register G2P plugin search paths on the process-level Manager.
            //    addPluginPath is idempotent (PluginFactory deduplicates paths
            //    per IID), so repeat calls are safe.
            auto *mgr = srt::g2p::Manager::instance();
            if (!mgr) {
                return srt::core::Error::inferenceError(
                    srt::core::ErrorCode::InferenceNotInitialized,
                    "setupG2pOnnxDriver: G2P Manager singleton is not available");
            }
            for (const auto &path : g2pPluginPaths) {
                mgr->addPluginPath(srt::g2p::kTaskPluginIid, path);
                mgr->addPluginPath(srt::g2p::kDriverPluginIid, path);
            }

            // 2. Locate the inference "dsdriver" object in the Runtime's
            //    "inference" category (registered by setupOnnxInferenceDriver).
            //    Do NOT auto-fallback: report InferenceNotInitialized so the
            //    caller can decide how to handle the missing driver.
            auto *inferenceCat = runtime.moduleCategory("inference");
            if (!inferenceCat) {
                return srt::core::Error::inferenceError(
                    srt::core::ErrorCode::InferenceNotInitialized,
                    "setupG2pOnnxDriver: 'inference' module category is not available; "
                    "call srt::driver::setupOnnxInferenceDriver() first");
            }
            auto driverObj = inferenceCat->getFirstObject("dsdriver");
            if (!driverObj) {
                return srt::core::Error::inferenceError(
                    srt::core::ErrorCode::InferenceNotInitialized,
                    "setupG2pOnnxDriver: inference 'dsdriver' object is not registered; "
                    "call srt::driver::setupOnnxInferenceDriver() first");
            }
            auto onnxDriver = driverObj.as<srt::driver::InferenceDriver>();
            if (!onnxDriver) {
                return srt::core::Error::inferenceError(
                    srt::core::ErrorCode::InferenceNotInitialized,
                    "setupG2pOnnxDriver: 'dsdriver' object is not an InferenceDriver");
            }

            // 3. Wrap the driver with a CPU-only SessionFactory adapter. The
            //    shared_ptr keeps the driver alive for the Manager's lifetime.
            auto factory =
                srt::core::NO<G2pOnnxSessionFactory>::create(onnxDriver);

            // 4. Register the adapter in the Manager's kDriverCategory under
            //    kG2pOnnxDriverName. Remove any previously-registered factory
            //    first so repeat calls replace rather than append (idempotency:
            //    getFirstObject returns the first registered object, so without
            //    removeObjects a re-call would leave the stale factory first).
            auto *driverCat = mgr->category(srt::g2p::kDriverCategory);
            if (!driverCat) {
                return srt::core::Error(
                    srt::core::ErrorCode::SessionError,
                    "setupG2pOnnxDriver: G2P 'driver' category is not available");
            }
            driverCat->removeObjects(srt::g2p::kG2pOnnxDriverName);
            driverCat->addObject(srt::g2p::kG2pOnnxDriverName, factory);

            return srt::core::Expected<void>{};
        } catch (const std::exception &e) {
            // CODING-02 / ROBUST-02: do not let exceptions cross the public
            // API boundary. Convert to a generic Error (General category,
            // 0-99 range) so callers always receive an Expected.
            return srt::core::Error(
                srt::core::ErrorCode::Unknown,
                std::string("setupG2pOnnxDriver: unexpected exception: ") + e.what());
        }
    }

} // namespace srt::g2p
