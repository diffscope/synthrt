#include "OnnxSession.h"

#include <stdcorelib/pimpl.h>

#include "internal/Env.h"
#include "internal/Session.h"

namespace srt::driver::onnx {

    class OnnxSession::Impl {
    public:
        Impl() : sessionId(Env::nextId()) {
        }
        ~Impl() {
        }

        int64_t sessionId;
        ::srt::driver::onnx::Session session;
    };

    OnnxSession::OnnxSession() : m_impl(std::make_unique<Impl>()) {
    }

    OnnxSession::~OnnxSession() {
        auto &impl = *m_impl;
        // TD-08: 显式 close，确保 ORT session 资源释放。析构期错误不传播
        // （ROBUST-05 不适用析构，析构抛异常是 UB）。
        if (impl.session.isOpen()) {
            auto closeExp = impl.session.close();
            if (!closeExp) {
                // 析构期无法返回错误，仅记录日志（如果有 Log 通道）
                // 注意：不要在析构中调用 Log.srtCritical（可能依赖已析构的全局对象）
                // 静默忽略是析构期的合理策略
            }
        }
    }

    srt::core::Expected<void>
        OnnxSession::open(const std::filesystem::path &path,
                          const srt::core::NO<srt::driver::InferenceSessionOpenArgs> &args) {
        auto &impl = *m_impl;
        auto openArgs = args.as<SessionOpenArgs>();
        if (!openArgs) {
            return srt::core::Error{
                srt::core::Error::InvalidArgument,
                "session open args is null pointer",
            };
        }
        // TD-08: 重复 open 时先 close 旧 session，避免资源泄漏
        if (impl.session.isOpen()) {
            auto closeExp = impl.session.close();
            if (!closeExp) {
                return closeExp.takeError();
            }
        }
        return impl.session.open(path, openArgs);
    }

    bool OnnxSession::isOpen() const {
        auto &impl = *m_impl;
        return impl.session.isOpen();
    }

    srt::core::Expected<void> OnnxSession::close() {
        auto &impl = *m_impl;
        return impl.session.close();
    }

    int64_t OnnxSession::id() const {
        auto &impl = *m_impl;
        return impl.sessionId;
    }

    std::vector<std::string> OnnxSession::inputNames() const {
        auto &impl = *m_impl;
        return impl.session.inputNames();
    }

    srt::core::Expected<srt::core::NO<srt::core::TaskResult>>
        OnnxSession::start(const srt::core::NO<srt::core::TaskStartInput> &input) {
        auto &impl = *m_impl;
        return impl.session.run(input);
    }

    srt::core::Expected<void>
        OnnxSession::startAsync(const srt::core::NO<srt::core::TaskStartInput> &input,
                                const srt::core::ITask::StartAsyncCallback &callback) {
        auto &impl = *m_impl;
        return impl.session.runAsync(input, callback);
    }

    srt::core::NO<srt::core::TaskResult> OnnxSession::result() const {
        auto &impl = *m_impl;
        return impl.session.result();
    }

    bool OnnxSession::stop() {
        auto &impl = *m_impl;
        return impl.session.terminate();
    }

}
