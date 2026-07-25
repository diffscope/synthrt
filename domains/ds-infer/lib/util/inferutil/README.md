# InferUtil — ds-infer 内部共享工具库

> AD-09 跨边界契约说明。本文件为代码就近文档，非对外发布契约。

## 定位（依据 INFRA-01 / ARCH-02）

`inferutil` 是 `ds-infer` 域的**内部通用实现库**（INFRA-01：通用实现进 `lib`），
**不是**公共 Level API（ARCH-02：公共头文件即契约）。其头文件位于
`include/inferutil/`，但该 `include` 仅供本库自身及同源构建的下游使用，
**不随 `srt-ds::infer` 安装**（见下文 CMake 契约）。

命名空间 `ds::infer::inferutil` 为域内私有命名空间，不属于
`design-guidelines.md` CODING-01 列出的公共命名空间
（`srt::core` / `srt::g2p` / `srt::svs` / `ds::bank` / `ds::infer`）。

## 跨边界使用现状

`inferutil` 在 ds-infer 域内部使用之外，被同源 in-tree 的 5 个推理插件跨边界引用：

| 头文件              | acoustic | duration | pitch | variance | vocoder | 域内 unittests |
| ------------------- | :------: | :------: | :---: | :------: | :-----: | :------------: |
| `Driver.h`          |    ✓     |    ✓     |  ✓    |    ✓     |   ✓     |       -        |
| `PluginCommon.h`    |    ✓     |    ✓     |  ✓    |    ✓     |   ✓     |       ✓        |
| `Algorithm.h`       |    ✓     |    ✓     |  ✓    |    ✓     |   -     |       ✓        |
| `InputWord.h`       |    ✓     |    ✓     |  ✓    |    ✓     |   -     |       ✓        |
| `LinguisticEncoder.h` |   -     |    ✓     |  ✓    |    ✓     |   -     |       ✓        |
| `SpeakerEmbedding.h`  |    ✓     |    -     |  ✓    |    ✓     |   -     |       ✓        |
| `Speedup.h`         |    ✓     |    -     |  ✓    |    ✓     |   -     |       ✓        |
| `TensorHelper.h`    |    ✓     |    -     |  -    |    -     |   -     |       ✓        |
| `ErrorCollector.h`  |    ✓¹    |    -     |  -    |    -     |   ✓¹    |       ✓        |
| `Parser.h`          |    ✓¹    |    ✓¹    |  ✓¹   |    ✓¹    |   ✓¹    |       -        |
| `detail/Parser_impl.h` |  -     |    -     |  -    |    -     |   -     |       -        |

¹ 经对应 `*Interpreter.cpp` 引用。

**tools 不引用 inferutil**：`tools/dsinfer-cli` 仅链接 `srt-ds::infer` 公共 API，
不跨边界依赖 `inferutil`。AD-09 任务描述中"tools/dsinfer-cli/main.cpp include
inferutil/*"与实际代码不符（已核实）。

## 跨边界稳定性契约

### 契约性质

`inferutil` 与 plugins 的跨边界关系是**同源构建期契约**，不是发布期契约：

1. plugins 通过 `$<BUILD_INTERFACE:inferutil>` 链接（见各 plugin 的
   `CMakeLists.txt`），**仅构建树可见，不安装**。
2. plugins 与 ds-infer 在同一源码树内同步演进，不存在"plugins 升级 ds-infer
   时因 inferutil API 变更而破坏"的独立升级场景——二者始终一起编译。
3. plugins 运行期对外暴露的是 `srt::svs::Inference` 公共 Level API，不经过
   `inferutil`。`inferutil` 仅服务于插件 .cpp 的实现复用。

### 稳定性规则

尽管不是 Level 契约，为降低同源重构成本，对 `include/inferutil/` 下头文件
的变更遵循以下内部规则：

| 变更类型                         | 规则                                                                       |
| -------------------------------- | -------------------------------------------------------------------------- |
| 新增头文件 / 新增函数 / 新增重载 | 允许，无需特别通知。                                                        |
| 既有函数签名变更 / 删除          | **必须**同步修改全部 5 个 plugin 与 unittests，确保同源树编译通过。          |
| `namespace` 改名                 | **禁止**——`ds::infer::inferutil` 已是 v9 稳定命名空间，改动波及全部下游。     |
| `detail/` 下文件                 | 实现细节，无稳定性承诺，仅由 `Parser.h` 内部 include。                       |

### 头文件分层（内部约定，非 Level）

- **A 类（流程级模板/内联，5 plugins 共用）**：`PluginCommon.h`、`Driver.h`
  — 修改影响面最大，变更前优先考虑能否以新增重载/新增函数替代签名修改。
- **B 类（数据预处理工具，多 plugins 共用）**：`Algorithm.h`、`InputWord.h`、
  `LinguisticEncoder.h`、`SpeakerEmbedding.h`、`Speedup.h`、`TensorHelper.h`
- **C 类（解析器，经 Interpreter 共用）**：`Parser.h` + `ErrorCollector.h` +
  `detail/Parser_impl.h` — `Parser_impl.h` 仅由 `Parser.h` 包含，不可直接引用。

## 为什么不提升为公共 API（include/diffsinger/Infer/PluginUtil/）

AD-09 任务方向 1 建议"把 plugins 共用的部分提升为公共 API"。经评估**不采纳**，
理由：

1. **依赖泄漏**：`Driver.h`、`PluginCommon.h`、`LinguisticEncoder.h` 依赖
   `srt::driver::InferenceDriver`、`srt::driver::InferenceSession`、
   `srt::driver::onnx::SessionOpenArgs`/`SessionStartInput` 等 Driver/ONNX 实现
   细节。提升至 `include/diffsinger/Infer/` 会把这些非公共依赖暴露为公共契约，
   违反 INFRA-01（公共 API 在 `include`，通用实现进 `lib`）。
2. **虚假契约**：按 ARCH-02，公共头文件即契约，签名不得变更。inferutil 当前
   内容为流程级实现复用（CODING-05 >60% 重叠提取），其形态随 ONNX 推理流程
   演进而调整，冻结为 Level 契约会阻碍合理重构。
3. **无独立升级场景**：plugins 与 ds-infer 同源同树构建，`BUILD_INTERFACE`
   已正确表达"构建期内部依赖、不发布"的语义，提升为公共 API 不解决任何实际
   问题，反而引入契约负担。
4. **CODING-03 组合优于继承**：`PluginCommon.h` 等以自由模板/内联函数形式
   提供，已经是组合优于继承的形态，无需额外的公共 API 层包装。

## CMake 契约（见 `CMakeLists.txt`）

```
dsinfer_add_library(${PROJECT_NAME} STATIC NO_INSTALL
    ...
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include/diffsinger/Infer)
```

- `NO_INSTALL`：inferutil 不安装，仅构建树内可见。
- `INCLUDE`（PUBLIC 于本 target）：本 include 目录对本 target 及直接链接者可见。
- 下游 plugin 通过 `$<BUILD_INTERFACE:inferutil>` 链接，二者共同保证
  "仅同源 in-tree 构建可访问，不发布、不跨版本"。

## 关联文档

- `docs/design/design-guidelines.md` — INFRA-01、ARCH-02、CODING-05
- `domains/ds-infer/lib/CMakeLists.txt` — ds-infer 顶层库与 Util 子库关系
- `domains/ds-infer/lib/util/CMakeLists.txt` — util 子库索引
