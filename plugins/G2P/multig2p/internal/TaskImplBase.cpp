#include "TaskImplBase.h"

#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

#include <synthrt/Core/Support/ConfigAccessor.h>
#include <synthrt/Core/Support/Logging.h>
#include <synthrt/G2P/Base/LangCommon.h>
#include <synthrt/G2P/Task/G2pTask.h>

namespace srt::g2p::plugins::Multig2p::Internal {
    static srt::LogCategory Log("Multig2p");

    Multig2pTaskImplBase::Multig2pTaskImplBase(const srt::g2p::ModuleSpec *spec)
        : m_spec(spec) {
    }

    srt::core::Expected<void> Multig2pTaskImplBase::initialize() {
        if (!m_spec) {
            return srt::g2p::Error(srt::g2p::Error::NullPointerError,
                                   "Multig2p: module spec is nullptr");
        }

        // 1. 读取 config.json 的 inference 块
        auto cfg = srt::core::config(m_spec);
        const auto &raw = cfg.raw();
        if (const auto it = raw.find("inference"); it != raw.end() && it->second.isObject()) {
            const auto &inf = it->second.toObject();
            if (const auto i2 = inf.find("default_max_len"); i2 != inf.end() && i2->second.isInt()) {
                m_maxLen = static_cast<int>(i2->second.toInt());
            }
            if (const auto i2 = inf.find("default_beam_size"); i2 != inf.end() && i2->second.isInt()) {
                m_beamSize = static_cast<int>(i2->second.toInt());
            }
            if (const auto i2 = inf.find("default_top_k"); i2 != inf.end() && i2->second.isInt()) {
                m_topK = static_cast<int>(i2->second.toInt());
            }
            if (const auto i2 = inf.find("length_penalty"); i2 != inf.end() && i2->second.isNumber()) {
                m_lengthPenalty = static_cast<float>(i2->second.toDouble());
            }
            if (const auto i2 = inf.find("default_language"); i2 != inf.end() && i2->second.isString()) {
                m_defaultLangRef = i2->second.toString();
            }
        }

        // 2. 加载 bundle.json + vocabulary.json
        const auto bundleDir = m_spec->path();
        auto bundleExp = BundleLoader::loadBundleJson(bundleDir / "bundle.json");
        if (!bundleExp) {
            Log.srtCritical("Multig2p: failed to load bundle.json: %1",
                            bundleExp.error().message());
            return bundleExp.takeError();
        }
        auto bundleMeta = bundleExp.take();

        auto vocabExp = BundleLoader::loadVocabulary(bundleDir / "vocabulary.json");
        if (!vocabExp) {
            Log.srtCritical("Multig2p: failed to load vocabulary.json: %1",
                            vocabExp.error().message());
            return vocabExp.takeError();
        }
        m_vocab = vocabExp.take();
        m_langIdMap = LangIdMap::fromLanguages(bundleMeta.languages);

        // 3. 尝试获取 G2P ONNX 驱动
        auto runtime = m_spec->runtime();
        auto driverCat = runtime ? runtime->moduleCategory(srt::g2p::kDriverCategory) : nullptr;
        auto driverObj = driverCat
                             ? driverCat->getFirstObject(srt::g2p::kG2pOnnxDriverName)
                             : srt::core::NO<srt::core::NamedObject>();
        if (driverObj) {
            m_driver = driverObj.as<srt::g2p::SessionFactory>();
        }

        if (!m_driver) {
            Log.srtWarning("Multig2p: G2P ONNX driver '%1' not available, "
                           "inference will fall back to original lyrics",
                           std::string(srt::g2p::kG2pOnnxDriverName));
            m_driverAvailable = false;
            return {};
        }

        // 4. 打开 ONNX sessions（encoder / decoder_step_init / decoder_step）
        auto openSession = [&](const std::string &logicalName,
                               const std::string &sessionName)
            -> srt::core::Expected<srt::core::NO<srt::g2p::SessionTask>> {
            auto fileOpt = BundleLoader::resolveOnnxFile(bundleMeta, bundleDir, logicalName);
            if (!fileOpt) {
                return srt::g2p::Error(
                    srt::g2p::Error::ConfigError,
                    stdc::formatN("Multig2p: bundle missing '%1' file", logicalName));
            }
            auto session = m_driver->createSession();
            if (!session) {
                return srt::g2p::Error(
                    srt::g2p::Error::RuntimeError,
                    stdc::formatN("Multig2p: failed to create %1 session", sessionName));
            }
            auto openArgs = srt::core::NO<srt::g2p::SessionOpenArgs>::create();
            openArgs->useCpu = false;
            auto res = session->open(*fileOpt, openArgs);
            if (!res) {
                return srt::g2p::Error(
                    srt::g2p::Error::RuntimeError,
                    stdc::formatN("Multig2p: failed to open %1 session: %2",
                                  sessionName, res.error().message()));
            }
            return session;
        };

        auto encExp = openSession("encoder", "encoder");
        if (!encExp) {
            Log.srtCritical("%1", encExp.error().message());
            m_driverAvailable = false;
            return encExp.takeError();
        }
        m_encoderSession = encExp.take();

        auto initExp = openSession("decoder_step_init", "decoder_step_init");
        if (!initExp) {
            Log.srtCritical("%1", initExp.error().message());
            m_driverAvailable = false;
            return initExp.takeError();
        }
        m_decoderStepInitSession = initExp.take();

        auto stepExp = openSession("decoder_step", "decoder_step");
        if (!stepExp) {
            Log.srtCritical("%1", stepExp.error().message());
            m_driverAvailable = false;
            return stepExp.takeError();
        }
        m_decoderStepSession = stepExp.take();

        m_driverAvailable = true;
        Log.srtInfo("Multig2p initialized: %1 languages, beam_size=%2, max_len=%3",
                    m_langIdMap.languages().size(), m_beamSize, m_maxLen);
        return {};
    }

    std::string Multig2pTaskImplBase::getConfig() const {
        return {};
    }

    srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
    Multig2pTaskImplBase::makeFallbackResult(const std::vector<std::string> &words) const {
        auto g2pResult = srt::core::NO<srt::g2p::G2pResultV1>::create();
        g2pResult->g2pResult.reserve(words.size());
        for (const auto &lyric : words) {
            g2pResult->g2pResult.emplace_back(srt::g2p::G2pRes{
                std::string(lyric), std::string(m_spec->id()), std::string(),
                stdc::VersionNumber{}, std::string(lyric),
                std::vector<std::string>(), std::string(srt::g2p::kG2pModeCopy),
                srt::g2p::ModelInferenceFailed, std::string()});
        }
        return g2pResult;
    }

    srt::core::Expected<srt::core::NO<srt::core::ITensor>>
    getTensorFromResult(const srt::core::NO<srt::g2p::SessionResult> &result,
                        const std::string &name) {
        if (!result) {
            return srt::g2p::Error(srt::g2p::Error::RuntimeError,
                                   "session result is nullptr");
        }
        const auto it = result->outputs.find(name);
        if (it == result->outputs.end() || !it->second) {
            return srt::g2p::Error(
                srt::g2p::Error::RuntimeError,
                stdc::formatN("tensor '%1' not found in session result", name));
        }
        return it->second;
    }

}
