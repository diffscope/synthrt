# 契约与三元组重构计划

状态：身份类型、关联命名及单头文件布局已于 2026-11-17 经用户确认。第一阶段身份类型及三库调用方迁移已完成并通过验证，payload 与 API 迁移已实现并通过三库验证。本计划承接 docs/claude/contract-variant-plan.md，身份类型与目录布局以本文为准。

## 已确认的设计

公开身份类型只有两个：ContribContract 表示 interface 与 level，ContribContractTriplet 表示 contract 与 variant。两个类型统一定义在 synthrt/Core/ContribContract.h 中，不保留 ContribTriplet 类型和头文件，也不引入 static 身份类型。

两种类型均拥有字符串。契约与变体头文件分别使用 inline const Contract 和 inline const Triplet 常量，接受运行期初始化，不提供编译期常量保证。当前构造 payload、注册实现和校验身份的用途不要求额外的静态类型体系。

toString() 用于诊断文本，字段以逗号分隔，例如 com.example.Contract, level 2, variant native。该文本不定义序列化或解析协议。

相等比较始终比较完整身份。契约 payload 使用 payload.contract() == spec.triplet().contract()，configuration 使用 payload.triplet() == spec.triplet()。不提供契约与三元组之间的相等运算，也不需要额外的 matches() 接口。

exports、import options 与 runtime options 只持有契约身份。configuration 持有完整三元组。身份要求由类型确定，不使用空 variant 表达契约，不使用运行期标志选择身份校验规则。具体存储布局和公共基类的最小调整在实施时核对，不额外扩展一套公开身份层次。

## API 组织

契约头文件包含 Contract、exports、options、输入、结果及 executive。公开变体头文件包含 Triplet 与 Configuration。契约和变体头文件平铺在同一个 level 目录，例如 Acoustic/1/AcousticApiL1.h 和 Acoustic/1/AcousticOnnxApiL1.h。

类名去掉命名空间已经表达的契约前缀，例如 AcousticImportOptions 改为 ImportOptions。需要跨模块读取的 configuration 公开，仅供插件使用的 configuration 留在插件内部。Contract、Triplet 常量命名与身份迁移一起实施，并同步开发规范。

## 实施顺序

1. 在 ContribContract.h 中定义 ContribContract 和 ContribContractTriplet。将三库现有 ContribTriplet 类型迁移到 ContribContractTriplet，保留 triplet() 访问器，变体字符串使用 variant()。验证值比较、所有权和现有三元组行为。
2. 同步修改 payload、PackageLoader、synthrt/dsinfer 的契约和变体头文件、插件、CLI、测试与文档。契约校验只比较 interface 和 level，configuration 校验全部三项。核对 runtime options 的执行入口校验。
3. 迁移 wolf 的公开 API 和调用方。将 S2p 的符号保持判断从 variant 名称判断改为读取 exports 能力。实施前核对能力由实现生成还是由清单声明，并同步规范、解析器及相关脚本。
4. 迁移 otter 的公开 API、插件及调用方。
5. 用同一份新 synthrt 安装产物验证 wolf 和 otter，收尾文档及依赖来源，核对旧身份常量和构造方式的剩余实际出现次数。

每个提交必须独立构建并通过相应测试。payload 切换与受影响调用方按完整语义变更提交，不留下只能由后续提交修复的中间状态。三个仓库的最终版本需要配套升级。

## 验收

- 同契约不同 variant 的 exports 和 import options 可以通过身份检查。
- interface 或 level 不同的契约 payload 被拒绝。
- configuration 任意一项身份不同均被拒绝。
- 保留空 payload 的拒绝行为，错误信息准确表达期望的契约或三元组。
- runtime options 按契约校验，不再要求调用者知道 variant。
- S2p 能力由 exports 提供，消费方不再依赖 direct 名称判断。
- 新公开头文件及跨库调用方使用明确的新安装产物构建。

先确认 build 退出码为 0，再运行测试。新增测试使用 --report_level=short 核对断言实际执行。只格式化改动过的 C++ 文件，保留原有行尾。

## 范围与工作区

本轮不以旧审计第三节的异常退出、生命周期和输入边界隔离复现为前置条件。用户已明确这项工作不需要做，这不代表对应旧报告已被验证或关闭。per-import options 的执行消费、Task 并发语义、真实引擎验证另行安排。

核对到的工作区为 D:/GitHub/synthrt、E:/GitHub/wolf 和 E:/GitHub/otter。D:/GitHub/wolf 是另一份较旧代码，不能作为本轮迁移基线。实施前读取下游仓库约定并重新检查工作区状态。

## 第一阶段验证结果

2026-11-17：synthrt、wolf、otter 构建退出码均为 0。synthrt 的 71 项测试通过，wolf 的 40 项测试通过。otter 的 28 项测试通过，GameGgmlInterpreter 和 TifaGgmlInterpreter 的两项 RealEngine 测试跳过，无失败。test_ContribContract 的 4 个用例、30 条断言通过。

下游均使用 D:/GitHub/synthrt/build/contract-identity-install 中的新安装产物。安装目录只有 ContribContract.h，没有 ContribTriplet.h。三库源码中的旧类型 ContribTriplet、过渡类型 ContribContractVariant 均已清零。完整身份保留 triplet() 访问器，变体字符串使用 variant()。

以上为第一阶段验证记录。第二阶段按下文实施。

## 第二阶段实现

ContribSpecPayload 只提供虚 contract() 接口。ContribExports、ContribImportOptions、ContribRuntimeOptions 各自拥有契约，ContribConfiguration 拥有完整三元组并返回其中的契约引用，不重复保存身份字符串。加载器依据静态 payload 类型选择契约或三元组校验，runtime options 在执行入口按契约校验。

dsinfer 五个 ONNX configuration 与 DiffSinger OpenVpi configuration 移入同 level 下的独立头文件，分别使用 Onnx::Triplet、OpenVpi::Triplet。wolf 既有公开 LinguistConfiguration 移入 LinguistWolfApiL1.h 的 Wolf::Configuration。其余 wolf、otter configuration 继续保留在插件内部。契约统一提供 Contract，类名去掉契约前缀。Onnx 驱动身份仍由名称和版本组成，常量命名为 Name、Version。

wolf S2p 的 Exports::preservesSymbols 是解释器生成的能力，不增加清单字段。direct 声明 true，其他内置实现保持 false，wolflinguist 读取该能力。第三方实现可声明同一能力。

## 第二阶段验证结果

2026-11-17：三库构建退出码均为 0。synthrt 71 项通过，wolf 40 项通过，otter 28 项通过、2 项 RealEngine 测试跳过。test_SynthUnit 的 16 个用例和 321 条断言通过，test_InferenceInterpreter 的 2 个用例和 33 条断言通过，wolf test_S2pInterpreter 的 5 个用例和 38 条断言通过。7 个新增公开变体头文件分别以单独翻译单元通过语法编译。

源码中的 API_INTERFACE、API_LEVEL、API_VARIANT 实际出现次数分别从 synthrt 的 309、wolf 的 189、otter 的 153 降至 0。下游继续使用 build/contract-identity-install 的同一份新安装产物。SynthRT 的版本已升至 0.1.0.0，wolf 与 otter 的私有 synthrt-main port 已指向本轮 SynthRT 提交，并将 port-version 升至 2。远端发布验证仍按独立工作项处理。
