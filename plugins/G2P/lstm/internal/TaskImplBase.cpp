#include "TaskImplBase.h"

#include <fstream>
#include <mutex>
#include <shared_mutex>

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>

#include <synthrt/Core/Support/ConfigAccessor.h>
#include <synthrt/Core/Tensor/Tensor.h>
#include <synthrt/Core/Support/Logging.h>
#include <synthrt/G2P/Core/Manager.h>
#include <synthrt/G2P/Task/Task.h>
#include <synthrt/G2P/Task/TaskPlugin.h>
#include <synthrt/G2P/Task/G2pTask.h>

namespace srt::g2p::plugins::LstmG2p::Internal
{
    // V1/V2 共用：从 SessionResult 的 outputs 中按名取张量
    srt::core::Expected<srt::core::NO<srt::core::ITensor>>
    getTensorFromResult(const srt::core::NO<srt::g2p::SessionResult> &result, const std::string &name) {
        const auto it = result->outputs.find(name);
        if (it == result->outputs.end()) {
            return srt::g2p::Error(srt::g2p::Error::RuntimeError,
                                   stdc::formatN("output '%1' not found in session result", name));
        }
        return it->second;
    }

    // Helper function to load phoneme mapping from JSON file
    srt::core::Expected<std::map<std::string, int>>
    LstmG2pTaskImplBase::loadPhonemeMapping(const std::filesystem::path &path, const std::string &fieldName) {
        std::map<std::string, int> out;

        std::ifstream file(path);
        if (!file.is_open()) {
            return srt::g2p::Error(
                srt::g2p::Error::FileSystemError,
                stdc::formatN(R"(error loading "%1": %2 file not found)", fieldName, stdc::path::to_utf8(path)));
        }

        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        if (size < 0) {
            return srt::g2p::Error(
                srt::g2p::Error::FileSystemError,
                stdc::formatN(R"(error loading "%1": failed to get file size of "%2")", fieldName,
                              stdc::path::to_utf8(path)));
        }
        std::string buffer(size, '\0');
        file.seekg(0);
        file.read(buffer.data(), size);

        std::string errString;
        const auto j = srt::core::JsonValue::fromJson(buffer, true, &errString);
        if (!errString.empty()) {
            return srt::g2p::Error(srt::g2p::Error::ConfigError, errString);
        }

        if (!j.isObject()) {
            return srt::g2p::Error(srt::g2p::Error::ConfigError,
                                   stdc::formatN(R"(error loading "%1": outer JSON is not an object)", fieldName));
        }

        const auto &obj = j.toObject();
        for (const auto &[key, value] : obj) {
            if (!value.isInt()) {
                return srt::g2p::Error(
                    srt::g2p::Error::ConfigError,
                    stdc::formatN(R"(error loading "%1": value of key "%2" is not int)", fieldName, key));
            }
            out[key] = static_cast<int>(value.toInt());
        }

        return out;
    }

    LstmG2pTaskImplBase::LstmG2pTaskImplBase(const srt::g2p::ModuleSpec *spec)
        : m_spec(spec) {}

    srt::core::Expected<void> LstmG2pTaskImplBase::initialize() {
        std::unique_lock lock(m_mutex);

        static srt::LogCategory Log("lstmG2p");

        // Get driver from G2P Manager's driver category (graceful degradation).
        // The g2pOnnxDriver is registered by the host (e.g. SynthrtEngine) as a
        // SessionFactory in the "driver" category before Manager::initialize().
        bool driverFound = false;
        auto driverCate = srt::g2p::Manager::instance()->category(srt::g2p::kDriverCategory);
        if (driverCate) {
            auto driverObj = driverCate->getFirstObject(srt::g2p::kG2pOnnxDriverName);
            if (driverObj) {
                m_driver = driverObj.as<srt::g2p::SessionFactory>();
                driverFound = true;
            }
        }

        if (!driverFound) {
            Log.srtWarning("ONNX driver unavailable: inference will be disabled for module '%1'. "
                                "Words will be returned as-is with DriverUnavailable error.", m_spec->id());
        }

        auto cfg = srt::core::config(m_spec);

        // Required fields - 存储到私有成员变量
        auto encoderExp = cfg.getResolvedPath("encoder");
        if (!encoderExp) {
            return encoderExp.takeError();
        }
        m_encoderPath = encoderExp.take();

        auto decoderExp = cfg.getResolvedPath("decoder");
        if (!decoderExp) {
            return decoderExp.takeError();
        }
        m_decoderPath = decoderExp.take();

        // Load charVocab
        auto charVocabPathExp = cfg.getResolvedPath("charVocab");
        if (!charVocabPathExp) {
            return charVocabPathExp.takeError();
        }
        m_charVocabPath = charVocabPathExp.take();
        auto charVocabMapping = loadPhonemeMapping(m_charVocabPath, "charVocab");
        if (!charVocabMapping) {
            return charVocabMapping.takeError();
        }
        m_charVocab = charVocabMapping.take();

        // Load phonemeVocab
        auto phonemeVocabPathExp = cfg.getResolvedPath("phonemeVocab");
        if (!phonemeVocabPathExp) {
            return phonemeVocabPathExp.takeError();
        }
        m_phonemeVocabPath = phonemeVocabPathExp.take();
        auto phonemeVocabMapping = loadPhonemeMapping(m_phonemeVocabPath, "phonemeVocab");
        if (!phonemeVocabMapping) {
            return phonemeVocabMapping.takeError();
        }
        m_phonemeVocab = phonemeVocabMapping.take();

        for (const auto &[phoneme, index] : m_phonemeVocab)
            m_idxToPhoneme[index] = phoneme;

        // Only open sessions if driver is available
        if (!driverFound) {
            m_driverAvailable = false;
            return {};
        }

        m_encoderSession = m_driver->createSession();
        const auto encoderOpenArgs = srt::core::NO<srt::g2p::SessionOpenArgs>::create();
        encoderOpenArgs->useCpu = false;
        if (auto res = m_encoderSession->open(m_encoderPath, encoderOpenArgs); !res)
            return res;

        m_decodeSession = m_driver->createSession();
        const auto predictorOpenArgs = srt::core::NO<srt::g2p::SessionOpenArgs>::create();
        predictorOpenArgs->useCpu = false;
        if (auto res = m_decodeSession->open(m_decoderPath, predictorOpenArgs); !res)
            return res;

        m_driverAvailable = true;
        return {};
    }

    std::string LstmG2pTaskImplBase::getConfig() const {
        std::shared_lock lock(m_mutex);

        // 返回缓存的配置
        if (!m_config.empty()) {
            return m_config;
        }

        // 从私有成员变量生成配置 JSON
        srt::core::JsonObject configObj;

        // 添加 configuration 对象
        srt::core::JsonObject configuration;
        configuration["encoder"] = srt::core::JsonValue(stdc::path::to_utf8(m_encoderPath));
        configuration["decoder"] = srt::core::JsonValue(stdc::path::to_utf8(m_decoderPath));
        configuration["charVocab"] = srt::core::JsonValue(stdc::path::to_utf8(m_charVocabPath));
        configuration["phonemeVocab"] = srt::core::JsonValue(stdc::path::to_utf8(m_phonemeVocabPath));

        configObj["configuration"] = srt::core::JsonValue(configuration);

        // 生成 JSON 字符串
        auto json = srt::core::JsonValue(configObj).toJson(2);

        return json;
    }

    srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
    LstmG2pTaskImplBase::makeFallbackResult(const std::vector<std::string> &lyrics) const {
        auto g2pResult = srt::core::NO<srt::g2p::G2pResultV1>::create();
        g2pResult->g2pResult.reserve(lyrics.size());
        for (const auto &lyric : lyrics) {
            g2pResult->g2pResult.emplace_back(srt::g2p::G2pRes{
                std::string(lyric), std::string(m_spec->id()), std::string(), stdc::VersionNumber{}, std::string(lyric),
                std::vector<std::string>(), std::string(srt::g2p::kG2pModeCopy),
                srt::g2p::DriverUnavailable, std::string()});
        }
        g2pResult->errorMessage = "ONNX driver unavailable, returning original lyrics";
        return g2pResult;
    }

    } // namespace srt::g2p::plugins::LstmG2p::Internal
