# 项目状态

本项目仍处于预发布阶段。[DS Spec 2.4](ds-spec-2.4.md) 与 C++ API 正在同步设计。在二者稳定之前，破坏性变更不会提供弃用过渡期。

## Package 运行时

SynthRT 的 Package 基础设施目前已经实现：

- DS Spec 2.4 清单，包括根字段 `contributions`、运行时 level、Package 依赖、字符串变量、多语言数据映射和 Contribution Locator
- `PackageHandle`、`PackageDependency`、`ContribLocator`、`ContribCategory`、`ContribSpec`、类型化 Contribution Payload 和 Import Binding
- `DataOnly` 清单读取，不发现插件，也不创建已提交的运行时状态
- 内部 Probe、依赖选择、Contribution 绑定和有序 Interpreter 发现
- Acquire 与 Ready 验证，以及随后的原子 Commit 可见性
- 共享 Package 身份、稳定的插件选择和基于强引用的 Package 释放
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

插件发现与 Package 加载流程已经可用，但完整的运行时执行生命周期尚未闭合。

## dsinfer 迁移

已经完成的迁移包括：

- 将库文件命名为 `synthrt-dsinfer`，同时保留 `dsinfer::dsinfer` CMake Target
- 基于 stdcorelib Allocator 和 View 的类型化 Tensor 存储
- 自己持有标识符文本的 ParamTag
- 公开的 Inference Driver、Session、Interpreter 和 Singer Provider 接口
- ONNX Driver Factory 和基于 Backend 的插件元数据
- 支持内部加载 ONNX Runtime，或借用由外部持有生命周期的 ONNX Runtime API
- 修订后的 level 1 推理契约和已经迁移到 2.4 的示例 Package 结构
- 主要基于公开 Runtime API 的命令行集成

仍待完成的迁移包括：

- ONNX Driver 手动测试仍在使用已经移除的 `PackageRef` 和 `UNO` API
- 推理执行入口仍包含尚未实现的 `NotImplemented` 路径
- Runtime Instance 计数和 Import Binding 执行仍需与 Package 卸载语义连接
- 部分 Utility 和命令行行为需要在公开 API 稳定后继续清理

## 后续工作

1. 确认并提交重构后的 README。
2. 创建 `refactor-v2/onnxruntime` 分支。
3. 将 `onnxdriver` 重构为职责和所有权明确的小型组件，并统一 SynthRT 与 dsinfer 的命名、注释、错误处理和日志风格。
4. 使用自动化测试覆盖 ONNX Runtime 加载、外部 API 生命周期、Environment 创建、Execution Provider 选择、Session 创建、Tensor 转换和失败路径。
5. 从 Driver 手动测试和 Utility 中移除剩余的过时 API。
6. 完成 Interpreter 执行，并连接 `ContribExecInstance`、`ContribImportBinding`、quit、wait 和 Package 释放行为。
7. 在目录 Package Loader 之外独立实现 `.dspk` 安装。
8. 稳定 DS Spec 2.4，并发布面向使用者的 Package 与插件开发文档。
