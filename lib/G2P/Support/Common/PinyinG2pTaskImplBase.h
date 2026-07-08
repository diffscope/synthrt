#ifndef SRT_G2P_PLUGINS_COMMON_PINYING2PTASKIMPLBASE_H
#define SRT_G2P_PLUGINS_COMMON_PINYING2PTASKIMPLBASE_H

#include <synthrt/G2P/Task/VersionedTaskImplBase.h>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>
#include <filesystem>

#include <synthrt/G2P/Task/G2pTask.h>
#include <synthrt/G2P/Core/PackageManager.h>
#include <synthrt/Core/Support/ConfigAccessor.h>
#include <InferUtil/Verifier.h>
#include <cpp-pinyin/G2pglobal.h>
#include <cpp-pinyin/PinyinRes.h>

namespace srt::g2p::plugins::Common
{

class PinyinG2pTaskImplBase : public srt::g2p::VersionedTaskImplBase {
public:
    struct Config {
        std::string dictPathKey;
        std::string languageName;
    };

    explicit PinyinG2pTaskImplBase(const srt::g2p::ModuleSpec *spec, Config config);
    ~PinyinG2pTaskImplBase() override = default;

    srt::core::Expected<void> initialize() override final;

    srt::core::Expected<srt::core::NO<srt::g2p::TaskResult>>
    start(const srt::core::NO<srt::g2p::TaskInput> &input) override final;

    std::string getConfig() const override final;

protected:
    virtual srt::core::Expected<void> onInitializeEngine() = 0;
    virtual bool isEngineInitialized() const = 0;
    virtual std::vector<Pinyin::PinyinRes> doHanziToPinyin(
        const std::vector<std::string> &input) = 0;

    const srt::g2p::ModuleSpec *m_spec;
    std::filesystem::path m_dictPath;

private:
    static std::vector<std::vector<srt::g2p::G2pRes>>
    groupLyrics(const std::vector<srt::g2p::G2pRes> &input);

    Config m_langConfig;
    srt::core::NO<srt::g2p::G2pResultV1> m_result;
    std::unique_ptr<srt::g2p::plugins::InferUtil::Verifier> m_verifier;
    mutable std::shared_mutex m_mutex;
    mutable std::string m_config;
};

} // namespace srt::g2p::plugins::Common

#endif // SRT_G2P_PLUGINS_COMMON_PINYING2PTASKIMPLBASE_H
