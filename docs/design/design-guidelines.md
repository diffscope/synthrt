# SynthRT 设计准则

日期: 2026-07-10

定位: 本文档是 synthrt 项目的权威设计准则，从 v2/v3 重构中提炼，作为后续所有方案制定的核对基准。每次制定方案时，须先对照本文档检验合理性。

---

## ARCH 架构准则

### ARCH-01：插件职责单一

每个推理 stage 的插件只负责一个阶段的加载、推理和停止。fallback、跨 stage 调度不进入单个插件。vocoder 声库包与其他声库包同等对待，无特殊类型。

### ARCH-02：Level 锚定长期兼容性

公共头文件即契约。同一 Level 内不得修改函数签名、结构体公开字段语义、枚举值的含义。破坏性变更必须提升 Level。推荐 `Level=2` 对应 `Version=2.x.x`。

### ARCH-03：组合优于继承和封装转发

宿主层（如 ds-editor-lite 的 `SynthrtEngine`）直接组合 `VoicebankScanner`、`LanguageService`、`Runtime`、`SingerStageResolver`、`ModelSet`。synthrt 不新增 facade 或转发层。

### ARCH-04：直接句柄而非映射层

调用方直接使用 `SingerRef`、`StageSet`、`ModelSet`、`Inference`。不保留"旧类型 → 新类型"的映射表作为架构主线。

### ARCH-05：最小但完整的模型生命周期

`ModelSet::load/unload/stop/model/isLoaded` 必须覆盖 create → initialize → start → stop → unload 的完整生命周期。不提供隐蔽的生命周期管理（后台自动回收或引用计数自动卸载）。

ModelSet 的核心价值是**惰性按 stage 加载**与**独立卸载**：使调用方可按需加载某个 stage（如仅 duration），也可独立卸载某个 stage 释放显存（如卸载 vocoder 保留 acoustic）。

### ARCH-06：跨包解析

一个 singer 的五个 stage 可来自不同 package（如共用公共 vocoder 包）。`SingerStageResolver` 必须能从已加载的全局包集合中解析出每个 stage 的来源 spec，并记录来源包路径。

---

## ROBUST 健壮性准则

### ROBUST-01：Expected 传播错误

所有可能失败的组件 API 返回 `srt::core::Expected<T>`。不在应用层抛异常，不让第三方异常穿越模块边界。

### ROBUST-02：异常边界隔离

ONNX Runtime、JSON、文件系统等第三方库的异常在边界转换为 `Error`。catch 块必须覆盖 `std::exception`（不仅子类异常），且必须记录上下文或返回错误。

### ROBUST-03：外部指针和句柄必须防空

`InferenceSpec*`、`SingerSpec*`、`NO<Inference>` 在解引用前须检查。空值返回明确错误。

### ROBUST-04：句柄失效场景需文档化

`ModelSet` 持有期间，`Runtime` 必须保持相关 package 加载。卸载顺序：先 unload 模型，再卸载 package。

### ROBUST-05：出错必须显式报错

禁止以下隐式错误吞没模式：

- `continue` 跳过失败项且不记录/返回错误
- `(void)` 丢弃返回 `Expected` 的调用结果
- `bool` 返回值丢失错误详情（应改 `Expected<void>` 或通过 out-param 传递 `Error`）
- `catch (...)` 不设置 `*error` / 不返回错误
- `Q_ASSERT` 用于非关键校验（Debug 生效、Release 消失）
- `extern "C"` 函数不隔离 C++ 异常（C++ 异常穿越 extern "C" 边界是 UB）

---

## INFRA 基础设施准则

### INFRA-01：目录表达职责

公共 API 在 `include`，通用实现进 `lib`，一等领域能力进 `domains`（ds-bank/ds-infer），插件进 `plugins`，工具进 `tools`，单库测试进 `unittests`，跨组件测试进 `tests`。

### INFRA-02：不保留无期限兼容层

Forwarding headers、旧 target alias、旧 namespace adapter 只有当已有外部发布契约时才允许存在，且须有删除日期。v2 不需要为临时 API 保留兼容层。

### INFRA-03：测试分级

- L1 单组件测试不加载插件 DLL
- L2 可加载插件
- L3（dsinfer-cli 增强）模拟 lite 调用模式：扫描声库、解析 stage、惰性创建 ModelSet、单独推理、单独卸载

### INFRA-04：测试框架统一

全部模块使用 Catch2 v3。不引入 boost-test 或其他测试框架。测试目标命名 `tst-<module>` 或 `synthrt-unittest-<module>`。

### INFRA-05：依赖最小化

不引入非必要的外部依赖。新增依赖须经人工决策。已有依赖的清理（如 boost-test）在保证功能不变的前提下进行。

---

## PACK 包管理准则

### PACK-01：依赖显式声明

声库包、G2P 包、推理插件的依赖在 manifest 和 CMake 中显式表达，不依赖扫描顺序或隐式全局状态。

### PACK-02：声库和 G2P 路径配合

`VoicebankScanner` 只扫描 `desc.json`。`LanguageService`（`srt::g2p`）只处理 G2P 初始化和语言路由。二者通过 `packageId→directory` 值表协作，不互相递归扫描。

---

## CODING 编码准则

### CODING-01：语言与格式

- C++20，4 空格缩进，120 列宽度
- 头文件保护统一使用 `#pragma once`
- 命名：`I` 前缀接口，`m_` 前缀成员，`_impl` 私有实现
- namespace：`srt::core` / `srt::dependency` / `srt::driver` / `srt::audio` / `srt::s2p` / `srt::g2p` / `srt::svs` / `srt::extract` / `srt::c` / `ds::bank` / `ds::infer` / `ds::infer::inferutil` / `ds::session`

> **CS-03 冻结说明**（2026-07-25）：公共头文件（`include/`）中 `_` 前缀成员（如 `Module::_id`/`_package`、`InferenceSpec::Impl::_className`/`_apiLevel`、`SingerImport::_declaredPackage`、`VersionRange::constraints_`/`valid_`/`parseError_` 等）属历史公共契约，依 ARCH-02 同一 Level 内不得修改公开字段语义/命名。当前 Level=2，这些字段**冻结至 Level=3** 才能清理。本轮重构不动公共头文件 `_` 前缀成员，仅清理内部实现（`lib/`/`domains/`/`plugins/`/`tests/`）。

### CODING-02：错误处理分层

- 应用层：`Result<T>` / `Expected<T>` 传播错误
- 第三方边界：`try-catch` 仅用于捕获第三方异常并转换为 `Error`
- 业务逻辑：不抛异常
- `extern "C"` 边界：必须 try-catch 所有 `std::exception`

### CODING-03：路径操作

使用 `dsfw::PathUtils`（或 `stdc::path::to_utf8`）处理路径，不在日志/错误消息中直接 `path.string()`。

### CODING-04：异步与线程安全

- 超过 50ms 的操作使用 `QtConcurrent::run` / `AsyncTask` / `PipelineRunner`
- 禁止 `processEvents()`
- 线程安全使用 `std::mutex` / `std::atomic` / `std::shared_mutex`
- 资源管理使用 RAII，禁止 bare `new`/`delete`

### CODING-05：模块设计

- 单一职责：模块合并 >60% 重叠时合并，30-60% 时提取基类
- 接口依赖抽象，构造注入优于 ServiceLocator
- ServiceLocator 仅用于真正的全局服务
- 开闭原则：新增类/适配器，不修改稳定核心

### CODING-06：文件 I/O 隔离

内部文档模型 + `IFormatAdapter` 隔离所有外部格式。配置使用 `SettingsKey<T>`（Keys.h），存储在用户目录而非项目文件。
