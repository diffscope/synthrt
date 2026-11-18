# 代码编写原则

本文档规定 SynthRT 的代码组织与 C++ 编写原则，作为当前开发与代码审阅的依据。具体格式以仓库根目录的 `.clang-format` 为准。

通用工具配置与验证入口位于 [scripts/cpp-rules](../scripts/cpp-rules/README.md)。格式配置在该目录维护，仓库根目录保存供编辑器使用的同步副本。命名检查处于试验阶段，覆盖范围与限制见配置注释。其他规则目前按本文执行，自动化迁移后续进行。

## 目录与文件

两种工程布局的目录职责及新增类、测试、插件、辅助库和模块的放置规则见 [工程目录结构](DirectoryStructure.md)。新增内容须按该文档选择位置并登记构建入口。

### 库文件

- 公开头文件放在 `模块/include/模块/` 下，源文件放在 `模块/lib/` 下，按模块保持对应关系。例如，`synthrt/include/synthrt/Core/PackageHandle.h` 对应 `synthrt/lib/Core/PackageHandle.cpp`。
- 仅供实现使用的私有头文件放在对应的 `模块/lib/` 目录中，使用 `_p.h` 后缀。私有头文件会增加实现之间的耦合，应尽量少用。
- 仅供多个实现文件复用且不独立编译的实现片段可以使用 `.cpp.inc` 后缀。普通声明放在头文件中，普通实现放在 `.cpp` 文件中。

### 文件命名

- 文件名采用大驼峰命名，并与其中的主要类型一致，例如 `PackageHandle.h` 与 `PackageHandle.cpp`。
- 程序入口 `main.cpp` 保持小写。
- 文件名中的缩写遵守下文的缩写规则。

### 插件

- 插件放在 `模块/plugins/类别/插件目录/` 下。头文件与源文件平铺在插件目录中，可以再分子目录，但不分 `include/` 与 `src/`。
- 插件目录名是插件头文件的 include 命名空间，类别目录是 include 根。例如，引用插件头文件写作 `<onnxdriver/Session/Session.h>`。
- 一个仓库内所有插件的目录名必须互不相同，跨类别也一样，因为一个测试可能同时看到多个类别的 include 根。`cmake/AddPlugin.cmake` 中的 `synthrt_add_plugin_objects()` 在名字重复时让配置失败。
- 插件入口 `main.cpp` 只声明插件类并导出插件实例。
- 其余源文件由 `synthrt_add_plugin_objects()` 编成对象库 `<插件目标>_objects`。插件本身和 `tests/auto/plugins` 下的测试都链接它，使测试可以直接测插件内部，不必加载插件。
- 入口不进对象库，因为它导出插件实例。一个测试链接两个插件的对象时，入口会导致重复导出。

## C++ 命名

### 大小写

| 对象 | 规则 | 示例 |
|---|---|---|
| 类及其他类型 | 大驼峰 | `PackageHandle`、`JsonValue` |
| 枚举成员 | 大驼峰 | `DataOnly`、`Load` |
| 函数、参数、变量、命名空间 | 小驼峰 | `fromJson()`、`value` |
| 宏 | 全大写，单词以下划线分隔 | `SYNTHRT_EXPORT` |
| 常量 | 大驼峰 | `Contract`、`Triplet` |

- 表示二元操作左右两侧的 `LHS` 与 `RHS` 是全大写变量名的例外。
- 命名空间结束处不添加注释。

### 成员与全局变量

- 类的私有数据成员使用 `m_` 前缀，公有数据成员不使用前缀。
- PImpl 中相互关联的实现指针与声明对象指针是例外，使用与 stdcorelib 一致的 `_impl` 与 `_decl`。
- getter 使用所读取的属性名，例如 `value()`。
- setter 使用 `set` 加属性名，例如 `setValue()`。
- 全局非静态变量使用 `g_` 前缀，全局静态变量使用 `s_` 前缀。应尽量避免引入全局可变状态。

### 缩写与保留名称

- 文件名、类型名和函数名中的缩写按普通单词书写，只大写首字母，例如 `Support/Json.h`、`JsonValue`、`OnnxDriver`、`JsonValue::fromJson()`。这一点与 LLVM 不同，LLVM 把同样的头文件写作 `Support/JSON.h`。
- 接口类型的前缀 `I` 不是缩写，`ITask`、`ITensor` 保持原样。
- 宏仍使用全大写。常量使用大驼峰，本身为全大写缩写的 `IID`、`ID` 保留原样。既有常量随所属 API 迁移，不进行无关的全库改名。
- 模块目录 `SVS` 以及 `ExecutionProvider_CUDA.cpp`、`ExecutionProvider_DML.cpp` 保留原名。新增文件与类型不仿照这些保留名称命名。

## 格式与实现

### 格式化

- 所有改动过的 C++ 文件使用仓库的 `.clang-format` 格式化，只对自己改动的文件运行。
- 不手工制造与格式化配置相冲突的对齐或换行。
- 修改文件时保留原有行尾，避免引入无关的全文件变化。

### 命名空间

- 命名空间花括号内部的第一行和最后一行必须是空行。

### 类型推导与内联

- 初始化表达式的类型为指针时，使用 `auto name = ...`，不写 `auto *name = ...`。`auto` 会自动推导出指针类型，额外的 `*` 不提供信息。
- 短小且需要暴露定义的函数可以在类内实现，或在头文件的类定义之后使用 `inline` 实现。
- 不仅仅为了减少一个 `.cpp` 文件而把较长实现放进公开头文件。

### 继承

- 析构函数不使用 `override` 关键字。
- 头文件内继承的类不使用 `final`。
- 基类的 protected 虚函数在派生类中覆写后仍应保持 protected，不得收窄为 private。调用者通过基类暴露的公开入口控制行为，派生实现不另行公开一套入口。

## 注释与文档

### 注释形式

- 类型与非 private 声明使用 LLVM 风格的 `///` 文档注释，写在声明上方，不使用 `\brief`。
- 类的 private 成员对象、成员函数，以及 `.cpp` 中实现细节的注释使用普通 `//`。
- 私有头文件中的类型与非 private 声明仍使用 `///`。文件是否为私有头文件不影响上述规则。
- 使用 Doxygen 命令表达结构化含义：`\c` 标识符、`\a` 参数、`\note`、`\warning`，多词片段使用 `<tt>`。不使用反引号或引号代替这些标记。

### 注释内容与文体

- 解释约束、所有权、生命周期，以及当前实现必须如此设计的原因。
- 不记录代码以前的样子或修改历史，这些信息由版本控制保存。
- 使用美式英语。
- 不用破折号连接从句，不用分号代替应有的断句。
- 不擅自重命名既有标签或措辞。

### Markdown

- 段落不硬折行，一段写在一行。代码块和表格不受此限。
- 文件结尾不留多余空行。

## 头文件引用

### 通用要求

- 不依赖其他头文件偶然提供的传递引用。
- 不同来源的引用块之间留一个空行，同一引用块中的头文件来自同一个库。
- 引用块通常按以下顺序排列。源文件中的同名头文件与私有头文件具有更高优先级，见下文。

1. 系统库。
2. 标准库。
3. 第三方库。
4. 项目内被依赖的其他目标。
5. 当前目标内的头文件。

### 头文件中的引用

- 引用项目公开头文件时使用完整公共路径，例如 `<synthrt/Core/PackageHandle.h>`。
- 插件头文件引用同一插件的其他头文件时，同样使用插件命名空间下的完整路径，例如 `<onnxdriver/Session/SessionSystem.h>`。测试只包含其中一个头文件时也应能编译。
- 被引用的头文件与当前头文件位于同一目录，且具有预引入或自动生成等特殊用途时，可以使用双引号直接引用。

```cpp
#include <synthrt/Core/PackageHandle.h>
```

### 源文件中的引用

同一构建目标内的头文件一律使用双引号直接引用。引用块按以下顺序排列：

1. 与源文件同名的公开头文件及 `_p.h` 私有头文件，组成最上方的第一个引用块。
2. 系统库。
3. 标准库。
4. 第三方库。
5. 项目内其他目标的头文件。
6. 当前目标内的其余头文件，组成最底部的独立引用块。

```cpp
#include "PackageHandle.h"
#include "PackageHandle_p.h"

#include <sys/types.h>

#include <filesystem>
#include <memory>

#include <stdcorelib/str.h>

#include "SynthUnit.h"
#include "ContribLocator.h"
```

## 头文件保护

### 宏名生成

1. 按下表确定头文件的逻辑路径。
2. 将路径全部转为大写，路径分隔符、扩展名前的点及连字符等非标识符字符换为下划线。中间各级目录全部保留，文件名中的 `_p` 保留为 `_P`。
3. `#endif` 后的注释与保护名一致。

项目头文件统一使用 `#ifndef`、`#define` 和 `#endif` 组成的保护宏，不以 `#pragma once` 代替。第三方头文件保持上游形式。

该规则与 LLVM 编码规范中 `llvm/docs/CodingStandards.rst` 的 Header Guard 一节一致。

### 路径规则

| 头文件类别 | 逻辑路径 |
|---|---|
| 公开头文件 | include 路径 |
| 库内私有头文件 | 模块名与 `lib/` 之后的路径 |
| 插件头文件 | 模块名与 `plugins/` 之后的路径，各级目录都计入 |
| 测试辅助头文件 | 模块名、`TESTS` 与 `tests/auto/` 之后的路径 |
| 独立工具头文件 | 模块名、`TOOLS` 与 `tools/` 之后的路径，包含程序目录名 |
| 手动测试程序头文件 | 模块名、`TESTS_MANUAL` 与 `tests/manual/` 之后的路径，包含程序目录名 |
| `util/` 子库头文件 | 模块名与 `util/` 之后的路径，包含辅助库目录名 |
| 第三方头文件 | 沿用上游的保护名，不按本节改写 |

### 示例

| 文件路径 | 保护名 |
|---|---|
| `dsinfer/include/dsinfer/Api/Drivers/Onnx/OnnxDriverApi.h` | `DSINFER_API_DRIVERS_ONNX_ONNXDRIVERAPI_H` |
| `synthrt/include/synthrt/Core/PackageHandle.h` | `SYNTHRT_CORE_PACKAGEHANDLE_H` |
| `synthrt/lib/Core/PackageHandle_p.h` | `SYNTHRT_CORE_PACKAGEHANDLE_P_H` |
| `dsinfer/plugins/inferencedrivers/onnxdriver/Runtime/DriverContext.h` | `DSINFER_INFERENCEDRIVERS_ONNXDRIVER_RUNTIME_DRIVERCONTEXT_H` |
| `dsinfer/tests/auto/Inference/TestCaseLoader.h` | `DSINFER_TESTS_INFERENCE_TESTCASELOADER_H` |
| `dsinfer/tools/cli/SynthesisRunner.h` | `DSINFER_TOOLS_CLI_SYNTHESISRUNNER_H` |
| `dsinfer/tests/manual/onnxdriver/SessionRunner.h`（假设的手动测试头文件） | `DSINFER_TESTS_MANUAL_ONNXDRIVER_SESSIONRUNNER_H` |
| `dsinfer/util/inferutil/Algorithm.h` | `DSINFER_INFERUTIL_ALGORITHM_H` |
| `dsinfer/util/inputparser/InputParserCommon_p.h` | `DSINFER_INPUTPARSER_INPUTPARSERCOMMON_P_H` |
