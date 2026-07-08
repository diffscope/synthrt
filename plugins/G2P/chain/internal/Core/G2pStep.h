#ifndef SRT_G2P_PLUGINS_CHAING2P_G2PSTEP_H
#define SRT_G2P_PLUGINS_CHAING2P_G2PSTEP_H

#include <synthrt/Core/Module/Module.h>
#include <synthrt/Core/Support/Expected.h>
#include <synthrt/Core/Support/JSON.h>
#include <synthrt/G2P/Task/Task.h>
#include <synthrt/G2P/Package/Package.h>
#include "G2pContext.h"
#include <memory>
#include <string>
#include <vector>

namespace srt::g2p::plugins::ChainG2p
{
    /// G2pStep - G2p 处理步骤基类
    ///
    /// 所有处理步骤必须继承此类并实现以下方法：
    /// - configure(): 配置步骤
    /// - handle(): 处理输入
    /// - cleanup(): 清理资源
    class G2pStep {
    public:
        virtual ~G2pStep() = default;

        /// 配置步骤
        /// @param spec 模块规范
        /// @param config 配置对象
        /// @return 成功返回 Expected<void>::success()，失败返回错误信息
        virtual srt::core::Expected<void> configure(const srt::g2p::ModuleSpec *spec,
                                                    const srt::core::JsonObject &config) = 0;

        /// 处理输入
        /// @param context 处理上下文
        virtual void handle(G2pContext &context) = 0;

        /// 清理资源
        virtual void cleanup() {}

        /// 获取步骤名称
        /// @return 步骤名称
        virtual std::string name() const = 0;

        /// 设置 Task 管理器（用于访问其他 Task）
        /// @param task Task 对象
        void setTask(srt::g2p::Task* task) { m_task = task; }

    protected:
        const srt::g2p::ModuleSpec* m_spec = nullptr;
        srt::g2p::Task* m_task = nullptr;
    };

    /// G2pStepFactory - 步骤工厂
    class G2pStepFactory {
    public:
        /// 创建步骤
        /// @param stepType 步骤类型
        /// @return 成功返回步骤对象，失败返回错误信息
        static srt::core::Expected<std::shared_ptr<G2pStep>> create(const std::string &stepType);

        /// 获取所有支持的步骤类型
        /// @return 步骤类型列表
        static std::vector<std::string> supportedTypes();

        /// 获取所有支持的步骤类型的字符串表示
        /// @return 步骤类型列表的字符串表示
        static std::string supportedTypesAsString();

    private:
        /// 连接字符串
        /// @param strings 字符串列表
        /// @param delimiter 分隔符
        /// @return 连接后的字符串
        static std::string joinStrings(const std::vector<std::string> &strings, const std::string &delimiter);
    };

} // namespace srt::g2p::plugins::ChainG2p

#endif // SRT_G2P_PLUGINS_CHAING2P_G2PSTEP_H
