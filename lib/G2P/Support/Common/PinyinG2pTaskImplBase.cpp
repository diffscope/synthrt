#include "PinyinG2pTaskImplBase.h"

#include <filesystem>
#include <mutex>
#include <shared_mutex>

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>

#include <synthrt/Core/Module/Module.h>
#include <synthrt/G2P/Task/G2pTask.h>
#include <synthrt/Core/Support/ConfigAccessor.h>
#include <synthrt/Core/Support/Logging.h>

#include <cpp-pinyin/G2pglobal.h>

#include <inferutil/Verifier.h>

namespace srt::g2p::plugins::Common
{
    using namespace srt::g2p::plugins::InferUtil;

    srt::core::LogCategory pinyinG2pLog("g2p");

    PinyinG2pTaskImplBase::PinyinG2pTaskImplBase(const srt::g2p::ModuleSpec *spec, Config config)
        : m_spec(spec), m_langConfig(std::move(config)) {}

    srt::core::Expected<void> PinyinG2pTaskImplBase::initialize() {
        std::unique_lock lock(m_mutex);

        auto cfg = srt::core::config(m_spec);

        auto verifyEntryExp = ParseVerifyEntries(cfg.raw(), m_spec->path());
        if (!verifyEntryExp) {
            return verifyEntryExp.takeError();
        }

        auto verifierExp = Verifier::Create(verifyEntryExp.take());
        if (!verifierExp) {
            return verifierExp.takeError();
        }
        m_verifier = verifierExp.take();

        auto dictPathExp = cfg.getResolvedPath(m_langConfig.dictPathKey);
        if (!dictPathExp) {
            return dictPathExp.takeError();
        }
        m_dictPath = dictPathExp.take();

        // Validate dictionary path before passing to cpp-pinyin.
        // ConfigAccessor::getResolvedPath() falls back to absolute() when
        // canonical() fails (e.g. path does not exist), so a non-existent
        // path can slip through. Without this check, the Pinyin::Pinyin()
        // constructor may hang indefinitely trying to load non-existent
        // dictionary files, leaving the G2P module stuck in Loading state.
        {
            std::error_code ec;
            if (!std::filesystem::exists(m_dictPath, ec) ||
                !std::filesystem::is_directory(m_dictPath, ec)) {
                return srt::g2p::Error(
                    ErrorCode::G2pInitializationError,
                    stdc::formatN("%1: dictionary path does not exist or is not a directory: %2",
                                  m_langConfig.languageName, stdc::path::to_utf8(m_dictPath)));
            }
            // Check directory is not empty (strong signal of deployment issue).
            bool hasFiles = false;
            for (const auto &entry : std::filesystem::directory_iterator(m_dictPath, ec)) {
                hasFiles = true;
                break;
            }
            if (ec || !hasFiles) {
                return srt::g2p::Error(
                    ErrorCode::G2pInitializationError,
                    stdc::formatN("%1: dictionary directory is empty or inaccessible: %2",
                                  m_langConfig.languageName, stdc::path::to_utf8(m_dictPath)));
            }
            pinyinG2pLog.srtInfo("%1: dictionary path resolved: %2",
                                 m_langConfig.languageName, stdc::path::to_utf8(m_dictPath));
        }

        Pinyin::setDictionaryPath(m_dictPath);

        pinyinG2pLog.srtInfo("%1: initializing cpp-pinyin engine...", m_langConfig.languageName);
        auto initResult = onInitializeEngine();
        if (!initResult) {
            return initResult;
        }
        pinyinG2pLog.srtInfo("%1: cpp-pinyin engine initialized", m_langConfig.languageName);

        // Build config inline instead of calling getConfig(): initialize()
        // already holds a unique_lock on m_mutex, and getConfig() tries to
        // acquire a shared_lock on the same m_mutex — std::shared_mutex does
        // NOT support recursive unique-then-shared locking on the same thread,
        // so calling getConfig() here would deadlock.
        pinyinG2pLog.srtInfo("%1: building config inline", m_langConfig.languageName);
        {
            srt::core::JsonObject configObj;
            srt::core::JsonObject configuration;
            configuration[m_langConfig.dictPathKey] =
                srt::core::JsonValue(stdc::path::to_utf8(m_dictPath));
            configObj["configuration"] = srt::core::JsonValue(configuration);
            m_config = srt::core::JsonValue(configObj).toJson(2);
        }
        pinyinG2pLog.srtInfo("%1: config built, checking engine", m_langConfig.languageName);

        if (!isEngineInitialized())
            return srt::g2p::Error(ErrorCode::G2pInitializationError,
                                   stdc::formatN("%1: cpp-pinyin library failed to initialize", m_langConfig.languageName));

        pinyinG2pLog.srtInfo("%1: initialize() returning success", m_langConfig.languageName);
        return {};
    }

    std::vector<std::vector<srt::g2p::G2pRes>>
    PinyinG2pTaskImplBase::groupLyrics(const std::vector<srt::g2p::G2pRes> &input) {
        std::vector<std::vector<srt::g2p::G2pRes>> groups;
        std::string lastMode;

        for (const auto &item : input) {
            if (groups.empty() || item.mode != lastMode) {
                groups.emplace_back();
                lastMode = item.mode;
            }
            groups.back().push_back(item);
        }

        return groups;
    }

    srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
    PinyinG2pTaskImplBase::start(const srt::core::NO<srt::g2p::TaskInput> &input) {
        {
            std::shared_lock lock(m_mutex);
            if (!isEngineInitialized())
                return srt::g2p::Error(ErrorCode::G2pRuntimeError,
                                       stdc::formatN("%1Task: chinese g2p not initialized", m_langConfig.languageName));
        }

        if (!input)
            return srt::g2p::Error(ErrorCode::G2pConfigError, "g2p input is nullptr");

        std::vector<srt::g2p::G2pRes> res;
        const auto g2pInput = input.as<srt::g2p::G2pInputV1>();

        const auto verifyRes = m_verifier->verify(g2pInput->g2pInput);
        res.reserve(verifyRes.size());
        for (const auto &[lyric, mode, error] : verifyRes) {
            srt::g2p::G2pErrorType wordErrorType = error ? srt::g2p::InvalidLyric : srt::g2p::NoError;
            res.emplace_back(srt::g2p::G2pRes{std::string(lyric), std::string(m_spec->id()), std::string(), stdc::VersionNumber{}, std::string(),
                                              std::vector<std::string>(), std::string(mode), wordErrorType, std::string()});
        }

        const auto groupLyric = groupLyrics(res);

        auto g2pResult = srt::core::NO<srt::g2p::G2pResultV1>::create();

        for (const auto &g2pResGroup : groupLyric) {
            const auto mode = g2pResGroup.front().mode;
            std::vector<std::string> _input;
            for (const auto &g2pRes : g2pResGroup)
                _input.push_back(g2pRes.lyric);

            if (mode != srt::g2p::kG2pModeConvert) {
                for (const auto &word : _input) {
                    srt::g2p::G2pRes newRes;
                    newRes.lyric = word;
                    newRes.g2pId = std::string(m_spec->id());
                    newRes.pronunciation = word;
                    newRes.candidates = std::vector<std::string>();
                    newRes.mode = std::string(mode);
                    newRes.errorType = srt::g2p::NoError;
                    g2pResult->g2pResult.emplace_back(newRes);
                }
                continue;
            }

            std::vector<Pinyin::PinyinRes> pinyinRes;
            try {
                pinyinRes = doHanziToPinyin(_input);
            } catch (const std::exception &e) {
                pinyinG2pLog.srtWarning(
                    "%1: doHanziToPinyin threw exception (mode=%2, count=%3): %4",
                    m_langConfig.languageName, std::string(mode), _input.size(), std::string(e.what()));
                for (const auto &word : _input) {
                    srt::g2p::G2pRes newRes;
                    newRes.lyric = word;
                    newRes.g2pId = std::string(m_spec->id());
                    newRes.pronunciation = word;
                    newRes.candidates = std::vector<std::string>();
                    newRes.mode = std::string(srt::g2p::kG2pModeCopy);
                    newRes.errorType = srt::g2p::UnknownError;
                    g2pResult->g2pResult.emplace_back(newRes);
                }
                continue;
            }

            for (auto &[hanzi, pinyin, candidates, conversionError] : pinyinRes) {
                srt::g2p::G2pErrorType wordErrorType = srt::g2p::NoError;
                if (conversionError) {
                    wordErrorType = srt::g2p::InvalidLyric;
                }
                srt::g2p::G2pRes newRes;
                newRes.lyric = std::string(hanzi);
                newRes.g2pId = std::string(m_spec->id());
                newRes.pronunciation = std::string(pinyin);
                newRes.candidates = std::vector<std::string>(candidates.begin(), candidates.end());
                if (newRes.candidates.empty() && !newRes.pronunciation.empty()) {
                    newRes.candidates.push_back(newRes.pronunciation);
                }
                newRes.mode = std::string(mode);
                newRes.errorType = wordErrorType;
                g2pResult->g2pResult.emplace_back(newRes);
            }
        }

        return g2pResult;
    }

    std::string PinyinG2pTaskImplBase::getConfig() const {
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (!m_config.empty()) {
                return m_config;
            }
        }

        srt::core::JsonObject configObj;

        srt::core::JsonObject configuration;
        configuration[m_langConfig.dictPathKey] = srt::core::JsonValue(stdc::path::to_utf8(m_dictPath));

        configObj["configuration"] = srt::core::JsonValue(configuration);

        auto json = srt::core::JsonValue(configObj).toJson(2);

        return json;
    }

} // namespace srt::g2p::plugins::Common
