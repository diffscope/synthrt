# 项目状态

本项目仍处于预发布阶段。[DS Spec 2.4](DsSpec2.4.md) 与 C++ API 正在同步设计。在二者稳定之前，破坏性变更不会提供弃用过渡期。

## Package 运行时

SynthRT 的 Package 基础设施目前已经实现：

- DS Spec 2.4 清单，包括根字段 `contributions`、运行时 level、Package 依赖、字符串变量、多语言数据映射和 Contribution Locator
- `PackageHandle`、`PackageDependency`、`ContribLocator`、`ContribCategory`、`ContribSpec`、类型化 Contribution Payload 和 Import Binding
- 以唯一 `role` 标识 Import，并同时提供保持清单顺序的遍历与直接按 role 查询
- `DataOnly` 清单读取，不发现插件，也不创建已提交的运行时状态
- 内部 Probe、依赖选择、Contribution 绑定和有序 Interpreter 发现
- Acquire 与 Ready 验证，以及随后的原子 Commit 可见性
- 共享 Package 身份、稳定的插件选择和基于强引用的 Package 释放
- `ContribExecutive` 对整棵监督树的递归停止与等待、析构时自动删除子树，以及由 Package 级实例计数和 Import Binding 共同构成的卸载流程
- 用于非 Package Contribution 设施的 Runtime Service，包括 Inference Driver

当前目录 Loader 负责读取已经安装的 Package 目录树。`.dspk` 归档安装仍将由独立组件实现。

## 插件系统

插件层目前已经实现：

- 每个 Category 独立的有序插件搜索路径
- 基于 stdcorelib-plugin 的 Bundle 发现
- 根据 `(interface, level, variant)` 选择 Contribution Interpreter Factory
- 将 Singer Provider 实现为普通 Contribution Interpreter
- 将 Inference Driver 作为 Runtime Service 发现，并通过 `metadata.backend` 选择
- 按 Category 专用目录部署插件 Bundle

插件发现、Package 加载、Import Binding 和 Executive 生命周期的 Core 机制已经可用。dsinfer Interpreter 与手动测试宿主已迁移到类型化 Executive 和 Singer Pipeline。

## dsinfer 迁移

已经完成的迁移包括：

- 将库文件、CMake 包与安装目录都命名为 `synthrt-dsinfer`，Target 为 `synthrt::dsinfer`
- 基于 stdcorelib Allocator 和 View 的类型化 Tensor 存储
- 自己持有标识符文本的 ParamTag
- 公开的 Inference Driver、Session、Interpreter 和 Singer Provider 接口
- ONNX Driver Factory 和基于 Backend 的插件元数据
- 支持内部加载 ONNX Runtime，或借用由外部持有生命周期的 ONNX Runtime API
- ONNX Runtime API 与 Environment 的进程内一致性、按 Driver 隔离的模型缓存以及由 Session 共同持有的运行时生命周期
- ONNX Session 的同步与异步执行、终止与等待、输入输出 Tensor 转换，以及回调期间销毁 Session 的生命周期边界
- 使用真实 ONNX 模型覆盖内部与外部 Runtime API、并发打开、同步与异步推理、错误输入和无效模型的自动化测试
- 已迁移到当前 Driver、Task 和 Tensor API 的 ONNX Driver 手动测试
- Duration、Pitch、Variance、Acoustic 和 Vocoder Interpreter 均已迁移为类型化 Executive，并将模型执行拆分为独立 Task
- Singer 使用 `SingerPipelineExecutive` 表达已知推理流水线，并通过开放的 Import role 与 `ContribExecutiveFactory` 创建子 Executive
- DiffSinger Provider 已接入新的 Pipeline、role 查询和目标契约校验
- `InferenceSpec`兼容性检查由目标 Interpreter 实现，默认接受任意组合。DiffSinger 在 Commit 前强制检查 Acoustic 与 Vocoder 的配置兼容性
- 修订后的 level 1 推理契约和已经迁移到 2.4 的示例 Package 结构
- `tests/manual` 提供不安装的 Package 安装器和推理运行器。推理运行器通过 Singer Pipeline 和 Import role 驱动完整同步推理链
- Duration、Pitch、Variance、Acoustic 和 Vocoder Task 通过通用 Task 异步执行器支持异步调用，并覆盖停止、等待、回调执行和回调内销毁
- Duration、Pitch、Variance 和 Acoustic Input Parser 继续作为独立协议 Utility 提供，并由共享字段、音符、控制曲线和错误输入的自动化测试覆盖
- ONNX Driver 原手动测试中的有效场景均已由自动测试覆盖，模型及生成脚本已迁入自动测试资源
- `srt::ITask` 统一同步/异步 claim、active-only 取消、callback 内再启动与销毁、异常捕获、等待屏障和最终状态分类。wolf、otter 与 dsinfer 不再各自维护一套 claim/generation 状态机。Task 用例已在插桩 synthrt 下连续执行 100 次 ThreadSanitizer 验证，统一规则见 [Task 执行与取消语义](codex/TaskLifecycle.md)
- Duration、Pitch、Acoustic 与 Variance Executive 会保存各自的 Import Options。`speakerMapping` 在构造 speaker embedding 或音素 speaker 混合前解析，Variance 的 `predictions` 按模型声明顺序筛选返回的参数，模型声明的全部预测参数仍作为输入提供
- dsinfer 五个推理契约的 typed `exports` payload 统一命名为 `Exports`，与 wolf 契约及 `ContribExports` 的词汇一致。dsinfer 版本升至 `0.0.2.0`

本阶段验证包括：

- 真实插件的 Package 加载、兼容性拒绝、Pipeline 创建、五阶段子实例执行、WAV 输出与失败后资源释放已有自动化覆盖。YOUSA 1.65 真实 Package 已通过 `tests/manual/runner` 完成五阶段推理并生成 WAV
当前完整 `all` 构建可以通过，69 个自动测试均已通过。wolf 的 41 项测试通过。otter 的 30 项测试包含两个真实引擎测试并全部通过。类与测试入口的对应关系见 [测试对应表](codex/TestCoverage.md)。

## 后续工作

1. 扩展真实模型的端到端验证。YOUSA 1.65 已通过 CPU ONNX 端到端验证。otter 的 game.cpp `v0.1.3` serve 协议和 tifa.cpp `v0.1.6` 适配已通过真实引擎测试；tifa.cpp `v0.2.0` 删除了 `--text-file` 与 `--dict-dir`，需要另做接受音素序列的迁移。
2. 为 CUDA 与 DirectML Execution Provider 增加可用硬件环境下的集成测试。
3. 在目录 Package Loader 之外独立实现 `.dspk` 安装。
4. 稳定 DS Spec 2.4，并发布面向使用者的 Package 与插件开发文档。
5. 为发行产物固定 `Phonetic-Suite-Eng` 门禁：在来源修订、转换流程、模型检查点、训练数据与再分发许可全部记录并复核之前，该套件不得进入发行产物。`Phonetic-Suite-Multi` 是否包含 Eng 的权重尚未核实，核实后一并纳入门禁。
6. 契约与变体 API 拆分已完成：契约 payload 使用 Contract，configuration 使用完整 Triplet，公开配置头文件按 level 平铺。三库代码、文档和测试已迁移。验证记录已归档到 `.cache/codex/20261121-1446-ContractVariantPlan.md`。
