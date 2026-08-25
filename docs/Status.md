# 项目状态

本项目仍处于预发布阶段。[DS Spec 2.4](ds-spec-2.4.md) 与 C++ API 正在同步设计。在二者稳定之前，破坏性变更不会提供弃用过渡期。

## Package 运行时

SynthRT 的 Package 基础设施目前已经实现：

- DS Spec 2.4 清单，包括根字段 `contributions`、运行时 level、Package 依赖、字符串变量、多语言数据映射和 Contribution Locator
- `PackageHandle`、`PackageDependency`、`ContribLocator`、`ContribCategory`、`ContribSpec`、类型化 Contribution Payload 和 Import Binding
- 以唯一 `role` 标识 Import，并同时提供保持清单顺序的遍历与直接按 role 查询
- `DataOnly` 清单读取，不发现插件，也不创建已提交的运行时状态
- 内部 Probe、依赖选择、Contribution 绑定和有序 Interpreter 发现
- Acquire 与 Ready 验证，以及随后的原子 Commit 可见性
- 共享 Package 身份、稳定的插件选择和基于强引用的 Package 释放
- `ContribExecInstance` 父子所有权、Package 级实例计数，以及由 `quit`、`wait` 和 Import Binding 共同构成的卸载流程
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

插件发现、Package 加载、Import Binding 和执行实例生命周期的 Core 机制已经可用。dsinfer Interpreter 已迁移到类型化 Exec Instance，但 CLI Pipeline 和异步执行仍未闭合。

## dsinfer 迁移

已经完成的迁移包括：

- 将库文件命名为 `synthrt-dsinfer`，同时保留 `dsinfer::dsinfer` CMake Target
- 基于 stdcorelib Allocator 和 View 的类型化 Tensor 存储
- 自己持有标识符文本的 ParamTag
- 公开的 Inference Driver、Session、Interpreter 和 Singer Provider 接口
- ONNX Driver Factory 和基于 Backend 的插件元数据
- 支持内部加载 ONNX Runtime，或借用由外部持有生命周期的 ONNX Runtime API
- ONNX Runtime API 与 Environment 的进程内一致性、按 Driver 隔离的模型缓存以及由 Session 共同持有的运行时生命周期
- ONNX Session 的同步与异步执行、终止与等待、输入输出 Tensor 转换，以及回调期间销毁 Session 的安全边界
- 使用真实 ONNX 模型覆盖内部与外部 Runtime API、并发打开、同步与异步推理、错误输入和无效模型的自动化测试
- 已迁移到当前 Driver、Task 和 Tensor API 的 ONNX Driver 手动测试
- Duration、Pitch、Variance、Acoustic 和 Vocoder Interpreter 均已迁移为类型化 Exec Instance，并将模型执行拆分为独立 Task
- Singer 使用 `SingerPipelineExecInstance` 表达已知推理流水线，并通过开放的 Import role 与 `ContribExecFactory` 创建子执行实例
- DiffSinger Provider 已接入新的 Pipeline、role 查询和目标契约校验
- 修订后的 level 1 推理契约和已经迁移到 2.4 的示例 Package 结构
- 主要基于公开 Runtime API 的命令行集成

仍待完成的迁移包括：

- 五类 Inference Task 的异步执行路径仍返回 `NotImplemented`
- CLI 仍由前端逐项创建推理对象，尚未改为通过 Singer Pipeline 和 role factory 驱动完整执行链
- 真实插件参与的 Package 加载、Pipeline 创建、子实例执行与 Package 卸载还缺少一条端到端自动化测试
- 部分 Utility 仍保留旧模型和兼容代码，需要在 Interpreter 迁移后继续清理

当前完整 `all` 构建可以通过，14 个自动测试均已通过。

## 后续工作

1. 将 CLI 改为创建 Singer Pipeline，并通过 Import role 创建和调用类型化推理执行实例。
2. 增加真实 2.4 Package 的端到端自动化测试，覆盖插件发现、Pipeline 执行、父子实例释放、Binding 关闭和 Package 卸载。
3. 完成仍返回 `NotImplemented` 的异步执行路径，并补充停止、等待和回调销毁测试。
4. 从 Utility 中移除剩余的过时 API，并把仍有价值的手动场景迁移到自动化测试。
5. 为 CUDA 与 DirectML Execution Provider 增加可用硬件环境下的集成测试。
6. 在目录 Package Loader 之外独立实现 `.dspk` 安装。
7. 稳定 DS Spec 2.4，并发布面向使用者的 Package 与插件开发文档。
