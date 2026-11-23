# 代码编写原则

本文档规定 SynthRT 的代码组织与 C++ 编写原则，作为当前开发与代码审阅的依据。具体格式以仓库根目录的 `.clang-format` 为准。

通用工具配置与验证入口位于 [scripts/cpp-rules](../scripts/cpp-rules/README.md)。**该规则集尚未完善，暂不启用**：CI 与提交检查都不调用它，命名检查等自动化规则暂不作为门槛。格式仍以仓库根目录的 `.clang-format` 为准，其他规则按本文执行，自动化迁移后续进行。

## 目录与文件

两种工程布局使用相同的职责划分，区别在于模块根与公开头文件根的位置。目录角色不等于构建目标，一个功能目录不必单独生成一个库。新增类、测试、插件、内部辅助库或模块时，按本节选择位置。现有目录的保留名称不因本节而重命名。

### 目录角色与布局

| 角色 | 职责 |
|---|---|
| 仓库根 | 项目入口、仓库级选项、依赖准备和模块选择 |
| 模块构建根 | 组织模块内的库、插件、工具、测试与安装 |
| 公开头文件根 | 下游编译时使用的接口，以库的 include 命名空间组织 |
| `lib/` | 主库实现及实现私有头文件，按功能划分目录 |
| `util/` | 主库或插件复用的内部辅助库 |
| `plugins/` | 按类别组织的运行时插件 |
| `tools/` | 独立可执行程序 |
| `tests/` | 自动测试、手动测试及测试辅助内容 |
| `cmake/` | 构建辅助函数与模块 |
| `scripts/` | 开发、依赖准备及维护脚本 |
| `docs/` | 开发规范、设计与使用文档 |

#### SynthRT：仓库内按模块组织

```text
<仓库>/
├── CMakeLists.txt
├── cmake/
├── synthrt/
│   ├── CMakeLists.txt
│   ├── conf.cmake
│   ├── include/synthrt/
│   ├── lib/
│   ├── tests/
│   └── synthrt-config.cmake.in
├── dsinfer/
│   ├── CMakeLists.txt
│   ├── conf.cmake
│   ├── include/dsinfer/
│   ├── lib/
│   ├── util/
│   ├── plugins/
│   ├── tools/
│   ├── tests/
│   └── synthrt-dsinfer-config.cmake.in
├── scripts/
└── docs/
```

- 每个模块拥有独立的构建入口、公开接口与实现树。只建立实际需要的目录，不要求每个模块都有插件、工具或内部辅助库。
- `lib/Core/`、`lib/SVS/` 等目录用于主库内的功能划分。当前 SynthRT 的主库不是按这些目录分别构建多个库。
- 新增仓库级模块时，采用与 `synthrt/`、`dsinfer/` 同级的模块根，并在仓库构建入口登记。

#### wolf：公开接口与实现树分开

```text
<仓库>/
├── CMakeLists.txt
├── include/wolf/
│   ├── Api/
│   ├── Linguist/
│   ├── Session/
│   └── Support/
├── src/
│   ├── CMakeLists.txt
│   ├── conf.cmake
│   ├── lib/
│   ├── util/
│   ├── plugins/
│   │   ├── inferenceinterpreters/
│   │   └── linguistproviders/
│   ├── tests/
│   └── synthrt-wolf-config.cmake.in
├── scripts/
└── docs/
```

- `src/` 是模块构建根，同时包含主库实现、内部辅助库、插件和测试，不只是 `.cpp` 文件的集合。
- 公开头文件位于仓库根的 `include/wolf/`，主库实现位于 `src/lib/`。`src/conf.cmake` 通过 `WOLF_INCLUDE_DIR` 指定公开头文件根。
- 新增功能通常放入现有主库的功能目录。只有需要独立构建、链接或安装边界时，才引入新的库或模块，不将每个新增类都拆成目标。

#### 角色对应表

下文中的 `<构建根>` 和 `<公开头文件根>` 按此表替换。`<功能>` 可以包含有明确职责的子目录。

| 角色 | SynthRT 模块式布局 | wolf 单库布局 |
|---|---|---|
| `<构建根>` | `<模块>/` | `src/` |
| `<公开头文件根>` | `<模块>/include/<模块>/` | `include/wolf/` |
| 主库实现根 | `<模块>/lib/` | `src/lib/` |
| 内部辅助库根 | `<模块>/util/` | `src/util/` |
| 插件根 | `<模块>/plugins/` | `src/plugins/` |
| 测试根 | `<模块>/tests/` | `src/tests/` |

### 新增代码的放置规则

#### 主库类与功能目录

1. 先判断类是否属于已有功能，优先放入对应目录。
2. 下游需要使用的声明放在 `<公开头文件根>/<功能>/<类型>.h`，实现放在 `<构建根>/lib/<功能>/<类型>.cpp`，两侧功能路径保持对应。
3. 实现私有声明放在对应的 `lib/<功能>/` 中，需要独立私有头文件时使用 `_p.h` 后缀，不放入公开头文件根。
4. 需要增加功能子目录时，明确其准入范围。目录名称应说明职责，避免把无关内容集中到笼统的目录中。

例如，SynthRT 的 `include/synthrt/Core/PackageHandle.h` 对应模块内的 `lib/Core/PackageHandle.cpp`。wolf 的 `include/wolf/Session/LinguistSession.h` 对应 `src/lib/Session/LinguistSession.cpp`。

#### 内部辅助库

- 多个实现或插件需要复用、且不属于安装接口的辅助代码放在 `<构建根>/util/<辅助库>/`。
- 辅助库的头文件与源文件直接放在 `<构建根>/util/<辅助库>/` 下，可以按职责增加子目录，不再分 `include/` 与 `src/`。
- 辅助库通过目标的公开 include 路径提供 `<构建根>/util/`，调用方写 `<辅助库/头文件.h>`。辅助库目录名是 include 命名空间，同一构建内须避免重名。
- 同一辅助库的源文件引用自身头文件时使用双引号，头文件引用其他辅助库头文件时使用完整 include 路径。第三方代码保留自己的目录布局。
- 调用目标私有链接辅助库。内部辅助库不因被多个目标使用而自动成为公开或安装接口。
- 必须由主库和插件共享同一份状态的对象，应由合适的共享库边界提供，不能仅为复用代码而复制进各个静态辅助库的使用者。

#### 插件

- 位置为 `<构建根>/plugins/<类别>/<插件目录>/`。头文件和源文件放在插件目录内，可以按职责分子目录，不再拆分 `include/` 与 `src/`。
- 插件目录名是其头文件的 include 命名空间。一个仓库内插件目录名跨类别也必须唯一。
- `main.cpp` 只声明插件类并导出实例。实现代码编入 `<插件目标>_objects`，供插件与内部测试共同链接。
- 新增插件须在对应类别的构建入口登记。类别入口由插件根的构建入口组织。

#### 独立工具

- 独立程序放在 `<构建根>/tools/<程序>/`，入口为 `main.cpp`。
- 只供该程序使用的文件留在程序目录内。需要被主库或插件复用的逻辑放入对应库，不通过链接可执行程序实现复用。

### 新增测试的放置规则

测试按被测代码的归属放置。内部实现测试和包加载等集成测试可以同时存在，分别验证各自的行为。

| 被测对象 | 位置 |
|---|---|
| 主库头文件 | `<构建根>/tests/auto/<功能>/test_<头文件名>.cpp` |
| 插件内部头文件 | `<构建根>/tests/auto/plugins/<类别>/<插件目录>/test_<头文件名>.cpp` |
| 内部辅助库 | `<构建根>/tests/auto/util/<辅助库>/test_<头文件名>.cpp` |
| 独立工具内部头文件 | `<构建根>/tests/auto/tools/<程序>/test_<头文件名>.cpp` |
| 测试专用插件 | `<构建根>/tests/auto/plugins/<类别>/<测试插件目录>/` |
| 需要人工操作的测试程序 | `<构建根>/tests/manual/<程序>/` |

- 默认一个头文件对应一个测试文件，文件名去掉 `.h` 后加 `test_` 前缀，例如 `PackageHandle.h` 对应 `test_PackageHandle.cpp`。头文件内的多个类型和自由函数放在同一测试文件中，不按类型另拆文件。
- `_p.h` 的测试归入对应头文件的测试文件，例如 `PackageHandle_p.h` 的测试放在 `test_PackageHandle.cpp`。
- 具体行为通常作为测试文件内部的测试用例命名，不以行为名代替测试文件的头文件名。
- 确需将某项行为拆成独立文件时，使用 `test_<头文件名>_<行为>.cpp`，保留被测头文件的归属。不能只写 `test_<行为>.cpp`。
- 主库测试的功能子目录与被测代码对应，不把插件内部测试放入主库功能目录。
- 插件测试的类别目录与被测插件的类别一致。测试专用插件也按其插件 IID 所属类别放置，例如 `inferenceinterpreters/stubinference/` 与 `singerproviders/stubsinger/`。
- 插件内部测试链接插件对象库，不链接插件入口。验证插件发现、加载或包绑定的测试使用真实的加载路径。
- 单个测试使用的辅助头文件和资源放在该测试目录附近。多个测试共享的内容放在最近的共同测试目录，不进入产品的公开接口或主库实现。
- 自动测试使用 `test_` 文件名前缀。当前 `add_auto_test()` 为每个测试源文件创建一个可执行程序，目标名与 CTest 名取自文件名，因此同一次构建内测试文件名必须避免重名。
- 新增测试须通过相应 `CMakeLists.txt` 注册，并确认该目录已接入上层构建入口。只有文件存在而未注册，不算已添加可运行测试。
- 依赖真实模型或外部资源的测试，应明确资源来源和缺失时的状态。不能把未执行的测试当成验证通过。

### 构建文件的职责

| 文件 | 职责 |
|---|---|
| 仓库根 `CMakeLists.txt` | 项目定义、仓库级选项、依赖查找及顶层模块组织 |
| `<构建根>/CMakeLists.txt` | 初始化模块构建系统，组织子目录并完成安装配置 |
| `<构建根>/conf.cmake` | 模块变量、统一目标配置及 qmsetup helpers 设置 |
| `lib/CMakeLists.txt` | 主库源文件、依赖、include 路径与导出属性 |
| 插件、辅助库或测试目录的 `CMakeLists.txt` | 该目录所属目标与测试的声明 |
| 包配置模板 | 安装后查找依赖及导入目标 |

- 公开接口需要的依赖公开链接，只供实现使用的依赖私有链接。
- 包配置模板应查找导出接口中引用的依赖，不能依赖消费方碰巧先行查找。
- 复用现有的 qmsetup 与 SynthRT 构建辅助函数，避免在每个插件或测试目录重新实现同一套规则。

### 新增内容的核对顺序

1. 确定所属库及公开、内部、插件或测试边界。
2. 根据角色对应表选择路径，并沿用对应功能目录与命名空间。
3. 在正确层级登记构建目标、测试和依赖，检查是否意外扩大安装或导出范围。
4. 格式化改动的 C++ 文件，确认构建退出码为 0 后再运行测试。
5. 新增自动测试时确认已注册并执行断言。Boost.Test 可使用 `--report_level=short` 核对断言结果。

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
| `dsinfer/tests/manual/runner/SynthesisRunner.h` | `DSINFER_TESTS_MANUAL_RUNNER_SYNTHESISRUNNER_H` |
| `dsinfer/tests/manual/onnxdriver/SessionRunner.h`（假设的手动测试头文件） | `DSINFER_TESTS_MANUAL_ONNXDRIVER_SESSIONRUNNER_H` |
| `dsinfer/util/inferutil/Algorithm.h` | `DSINFER_INFERUTIL_ALGORITHM_H` |
| `dsinfer/util/inputparser/InputParserCommon_p.h` | `DSINFER_INPUTPARSER_INPUTPARSERCOMMON_P_H` |
