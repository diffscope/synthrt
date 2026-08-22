# DiffSinger 数据格式与推理接口规范 2.4

> DiffSinger Data Format and Inference Interface Specification 2.4

此规范为 OpenVPI 为各种 AI 推理工具制定的标准，旨在为各种模型提供通用的组织结构与调用接口，使 AI 模型的分发与调用更为有序、规范。

本规范主要指导以下几种基础设施的开发：
1. 安装器（Installer）
2. 加载器（Loader）
3. 执行器（Executor）

## 0. 相对 2.3 的变化

| | 2.3 | 2.4 |
|---|---|---|
| `contributes` 的属性名 | `inferences` / `singers`（复数，固定两种） | 贡献类别名，**集合开放**；第三方类别使用反向域名 |
| `contributes` 的条目 | 对象，含 `id` `class` `configuration` | 对象，包含`id`与该类别自定的字段；贡献不一定是模块 |
| 运行时要求 | 无 | `desc.json`用`runtimeVersion`声明最低运行时版本 |
| 模块 ID | 写在模块自己的声明文件里 | **写在 `desc.json` 里，由 Package 赋予** |
| `class` | 推理类型 + 解释器选择，一词两义 | 改名 `interface`，只表示「这份声明遵循哪套契约」 |
| 实现变体 | 无，只能另起一个 `class` | 新增 `variant`，表示同一契约的不同实现；不同变体不保证可互换 |
| 术语 | Library（亦称 Package） | 统一用 **Package**，不再用 Library / lib |
| `$version` | 每份模块声明文件各写一份，`desc.json` 反而没有 | **只写在 `desc.json`**，全 Package 共用一个 |
| 模块声明文件的公共字段 | 各类别各自定义、各自解析 | 提取为《公共字段》，由框架统一解析 |
| `schema` | 名为 schema，装的却是功能清单 | 改名 `exports`，与 `imports` 成对 |
| `configuration`的语法 | 未区分契约与实现 | 完全由`variant`规定，契约不规定其中的公共字段 |
| 多语言的语言代码 | `zh_CN`，POSIX locale 写法 | **BCP 47**（`zh-CN`），并单列一节定义匹配规则 |
| 多语言路径 | 未明确落到具体字段 | `readme`、`avatar`、`background`与`demoAudio`均为多语言路径 |
| `imports` 条目 | `id` + `inferenceId` + `version` 三个字段，或一个裸字符串简写 | 一个 `ref` 引用串，**类别是其中一格**，不设简写 |
| `imports`集合语义 | 未规定 | 有序数组，重复引用是独立导入实例，不自动去重 |
| 引用中的版本 | `lib[version]/id`，版本语义未区分 | `=version` 精确匹配，`~=version` 兼容匹配 |
| 引用类型 | Package 与模块引用未分型，模块类别可以省略 | 分为 PackageReference 与基于它的 ModuleReference；持久化模块引用必须写类别 |
| 可选依赖 | `dependencies[].required`，默认 `true` | **不支持**，所有依赖均为强制依赖 |
| 依赖求解 | 候选选择与依赖图约束未规定 | 路径优先，同一路径选择最高兼容版本；允许多版本共存，禁止重复依赖、自依赖与环 |
| Package 兼容承诺 | `compatVersion`只参与版本区间判断 | 明确兼容区间内必须保持的公开表面，由 Package 作者负责 |
| DSPK 安全边界 | 未规定 | 解包严格封闭在安装目录内；运行时资源引用允许越过 Package root |
| 解释器发现 | 由解释器对象提供匹配信息 | 三元组写入插件元数据，无需加载插件即可选择；重复匹配取插件搜索顺序中的第一个 |
| interface 契约 | 只有名称与 API Level | 每个 (`interface`, `level`) 必须有稳定可定位的规范及 JSON Schema |
| 命令行工具 | 单独一章 | 移出本规范 |

---

## 1. 关于 Package（旧称 Library）

### 文件结构

本规范内，可分发的数据包的最小单位是 Package（包），是一个以`dspk`为扩展名的 ZIP 格式的压缩包。

压缩包内基本结构为：
```
+ xxx.dspk
  - desc.json
  - ...
```

Package 内多使用`json`作为声明文件，我们规定，声明文件中使用的相对路径的基路径是这个声明文件所在的目录。

#### 描述文件

`desc.json`是 Package 的描述文件，主要包括以下内容。

```json
{
    "$version": "1.0",
    "id": "foo",
    "version": "1.0.0.0",
    "runtimeVersion": "1.0",
    "compatVersion": "0.0.0.0",
    "vendor": "someone",
    "copyright": "Copyright (C) someone",
    "description": "Some package",
    "readme": "assets/readme.txt",
    "url": "https://www.example.com",
    "contributes": {
        "inference": [
            { "id": "pitch", "path": "./inferences/pitch/inference.json" },
            { "id": "variance", "path": "./inferences/variance/inference.json" }
        ],
        "singer": [
            { "id": "zhibin", "path": "./characters/zhibin/singer.json" }
        ]
    },
    "dependencies": [
        { "id": "bar", "version": "1.0.0.0" }
    ]
}
```

+ 必选字段
    + `$version`：**清单格式版本**，当前固定为`1.0`。它描述的是本规范所定义的文件格式，与下面的`version`无关。整个 Package 内的所有声明文件共用这一个版本号，模块的声明文件不再各自携带
    + `id`：唯一标识符，由 `segment` 以 `/` 连接而成（见下文引用文法）
    + `version`：版本号，格式见下文《版本》
    + `runtimeVersion`：加载该 Package 所需的最低运行时版本，格式见下文《版本》
+ 可选字段
    + `compatVersion`：兼容到的最低版本，如果为`0.0.0.0`表示向下兼容所有，如果与`version`相同则表示不向下兼容，缺省为`version`
    + `vendor`：提供者，可为多语言，见下文《多语言文本》
    + `copyright`：版权信息，可为多语言
    + `description`：介绍文字，可为多语言
    + `readme`：多语言路径，指向放置介绍、许可证等信息的文本文件
    + `url`：网站
    + `contributes`：功能贡献列表，见下节
    + `dependencies`：依赖的库，每个条目均为强制依赖，缺省为空
        + `id`：依赖库 ID
        + `version`：要求依赖 Package 兼容到的版本

`dependencies[].version`表示依赖方要求依赖 Package 兼容到的版本，不表示必须加载该精确版本。

加载器应在解析`contributes`之前先检查`$version`与`runtimeVersion`。遇到不认识的清单格式版本，或当前运行时版本低于`runtimeVersion`时，应当整包拒绝，而不是继续解析后面的字段。这也正是它们必须写在`desc.json`而非各模块声明文件里的原因——`desc.json`是最先被读到的文件。写在模块声明文件里的版本号，要等到整个 Package 已被接受、依赖已被解析之后才轮得到检查，那时拒绝已经太晚。

`dependencies`中的每个 Package 各自携带自己的`$version`，在其被加载时独立检查，不要求与本 Package 一致。

#### 版本

版本号由一至四个以`.`分隔的十进制非负整数组成：

```
version   = component *3("." component)
component = 1*DIGIT
```

比较前在右侧补`0`至四段，各段按整数比较。`1`、`1.0`、`1.0.0`与`1.0.0.0`因此表示同一版本。

`runtimeVersion`也按此规则规范化与比较。当前运行时版本大于或等于该值时满足运行时版本要求。

Package 的`compatVersion`不得大于`version`，否则清单无效。一个版本为`version`、最低兼容版本为`compatVersion`的 Package 兼容目标版本`target`，当且仅当：

```
compatVersion <= target <= version
```

声明兼容某个目标版本，表示当前 Package 可以直接替换该目标版本，而不要求依赖方修改声明。对兼容区间内已经发布的同 ID Package，当前 Package 必须保持以下公开表面：

- 已有贡献的类别、模块 ID 及其含义
- 已有模块的`interface`与`level`
- 已公开的`exports`能力及其语义
- 既有`options`写法及其语义
- 契约规定的运行时输入输出、单位、错误语义与资源所有权
- 贡献类别规定的必选字段及其兼容的数据类型和语义

当前 Package 可以增加贡献和可选能力，也可以修改不影响上述公开表面的实现细节、私有`configuration`、模型与算法。`variant`可以改变，但改变后仍须履行原有公开能力与运行时契约。任何破坏上述承诺的版本都必须将`compatVersion`提高到不再覆盖受影响的旧版本。

兼容性是 Package 作者作出的承诺。加载器只按版本区间判断候选，不负责读取或比较历史版本来证明该承诺。承诺不实属于 Package 缺陷，加载器或执行器在缺陷实际导致失败时应报告诊断。

#### 功能贡献

`contributes`的每个属性名是一个**贡献类别**，其值是该类别下的贡献条目数组。

```json
"contributes": {
    "inference": [
        { "id": "pitch", "path": "./inferences/pitch/inference.json" }
    ]
}
```

每个贡献条目：

+ 必选字段
    + `id`：贡献 ID，由 Package 赋予，同一类别下不得重复。对于模块贡献，它是别处引用该模块时使用的模块 ID，不是模块自身的属性，因此模块声明文件中不出现它。
+ 其余字段由该类别自行规定。

贡献不一定是模块。类别注册时必须声明其条目是否指向模块声明。模块类别的条目必须使用`path`指向包含公共模块字段的声明文件；`inference`与`singer`都是模块类别。非模块类别的条目不使用公共模块字段，其内容完全由类别规定，可以内联，也可以按该类别自己的规则引用其他文件。

> **为什么 `id` 在这里而不在模块里**
>
> 引用一个模块（见下文引用文法）解析的是「某个 Package 对它的称呼」。放在这里有三个后果：
>
> - 同一份模块目录被两个 Package 收录时，两边可以各自命名
> - 一个 Package 对外提供了什么，从它的`desc.json`一个文件就能列全
> - 解析一条引用不需要打开被引用模块的声明文件

#### 贡献类别是开放的

**本规范不规定贡献类别的完整集合。** `inference`与`singer`是本规范定义的两种，加载器实现可以再注册别的（例如语言资源）。

内置类别可以使用裸名称。第三方类别必须使用由其所有者控制的反向域名名称，例如`com.vendor.language`。同一个类别名称的条目 schema 与语义发布后必须保持兼容，不兼容的类别定义必须使用新名称。

加载器遇到不认识的类别时必须拒绝整个 Package。`runtimeVersion`可以防止旧运行时尝试加载依赖较新内置类别的 Package，但不能代替第三方类别的注册；声明了第三方类别的 Package 仍要求当前宿主已经注册该类别。

因此，贡献类别在框架层面是可以注册扩展的开放集合，但一次加载使用的类别必须全部已注册。增加新类别不保证未注册该类别的加载器仍能加载该 Package。

#### 依赖项

当前 AI 推理现状是，一个模型动辄超过 100MB 甚至 1GB，因此内存与显存是宝贵的，本规范使用了一种常见的方式缓解这个问题。

为了模块复用与增量更新，`dspk`引入了依赖机制。

- 模块复用：开发者要分发若干个`dspk`（假设为 A、B），但是它们之间存在一些可以复用的内容，为了节省存储时的硬盘资源以及推理时的内存资源，那么可以将这些内容独立到一个`dspk`（C）中，在 A 与 B 的描述文件中声明它们依赖 C 即可。

- 增量更新：开发者要分发一个`dspk`（A），A 中存在稳定与不稳定的部分，不稳定的部分每次更新都需要更改，为了节省网络资源，那么可以将不稳定的内容独立到一个`dspk`（B）中，每次更新时只需更新 B 即可。

### 多语言文本

面向人阅读的字段可以只写一个字符串，也可以写成一个对象，为不同语言各给一份：

```json
{
    "_": "Tadokoro Koji",
    "zh-CN": "李田所",
    "ja-JP": "田所浩二"
}
```

+ `_`**必选**，它是任何语言都没匹配上时的取值
+ 其余每个属性名是一个 **BCP 47** 语言标签，如`en`、`zh-CN`、`zh-Hant-TW`、`ja-JP`

只写一个字符串，等价于只有`_`一项的对象。

#### 匹配

前端每次向运行时传入一个 BCP 47 语言标签。运行时使用该标签按 **RFC 4647 §3.4 Lookup** 的截断规则查找，找不到就从右端逐段去掉子标签再找，直到只剩语言那一段，仍然找不到则取`_`。输入不是语言优先列表，不处理多个语言标签或`*`；无效的 BCP 47 标签属于调用方错误。

偏好`zh-Hant-TW`时依次尝试`zh-Hant-TW`、`zh-Hant`、`zh`，最后`_`。

截断后如果末尾留下 singleton，应将该 singleton 一并移除。例如`de-DE-x-goethe`依次尝试`de-DE-x-goethe`、`de-DE`、`de`，不会尝试`de-DE-x`。

标签匹配**不区分大小写**。多语言对象中除`_`外的属性名必须是有效 BCP 47 标签，并且不得出现大小写折叠后相同的两个标签，例如`zh-CN`与`ZH-cn`不能同时出现，否则声明无效。书写建议遵循 BCP 47 的惯例：语言小写，文字子标签首字母大写，地区全大写，如`zh-Hans-CN`。

> 2.3 中这里写的是`zh_CN`、`ja_JP`，那是 POSIX 的 locale 写法。改用 BCP 47 有两个原因：分隔符本应是连字符，而且 POSIX 那套表达不了文字子标签，区分不了`zh-Hans`与`zh-Hant`。

#### 用在哪些字段

标准字段的类型如下：

| 字段 | 类型 |
|---|---|
| `vendor`、`copyright`、`description` | 多语言文本 |
| `name` | 多语言文本 |
| `readme` | 多语言路径 |
| `avatar`、`background`、`demoAudio` | 多语言路径 |

贡献类别与契约追加的字段是否多语言，以及是文本还是路径，由定义它们的一方明确规定。

某个多语言字段的值是文件路径时，**每一个本地化的值各自解析**，基路径都是该声明文件所在的目录。

### 引用文法

PackageReference 指向一个 Package。ModuleReference 以 PackageReference 为基础，在其后追加`:category/contrib-id`以指向该 Package 中的模块；引用当前 Package 的模块时省略 PackageReference，但必须保留开头的`:`。

```
reference         = package-reference / module-reference
package-reference = package-id [ version-constraint ]
module-reference  = package-reference ":" category "/" contrib-id
                  / ":" category "/" contrib-id
version-constraint = "=" version / "~=" version

package-id    = segment *( "/" segment )
category      = segment *( "." segment )
contrib-id    = segment
segment       = 1*( ALPHA / DIGIT / "_" / "-" )
```

```
vendor/sample=1.0.0.0:inference/pitch    // 完整形式
vendor/sample~=1.0:inference/pitch       // 要求兼容 1.0.0.0
vendor/sample=1.0.0.0:singer/main
vendor/sample:singer/main                // 使用依赖求解选定的版本
:singer/main                             // 当前 Package
vendor/sample=1.0.0.0                    // 指向 Package 本身
```

+ ModuleReference 中没有 PackageReference 时，前导的`:`**不可省略**——`singer/main`指的是名为`singer/main`的 Package，不是`singer`类别下名为`main`的模块。
+ ModuleReference 必须包含 category。声明文件中不得使用省略 category 的持久化引用。
+ **类别是文法中的一格数据，而不是标点。** 新增一种贡献类别不需要改动这条文法。
+ 外部 ModuleReference 未写版本约束时，引用使用依赖求解已经选定的 Package 实例。
+ `=version`要求 Package 的实际`version`与给定版本相等。`~=version`要求 Package 兼容给定版本，按上文的兼容条件判断。
+ 引用当前 Package 的模块必须使用以`:`开头的形式。显式写出的 Package ID 始终表示外部 Package，并且必须在`dependencies`中声明。

### 安装与加载

#### 安装

对于`dspk`文件，安装就是在某个目录中解压它。本规范对这个目录没有任何要求。

##### 安全解包

ZIP 中的 entry 路径必须使用`/`作为分隔符，并满足以下要求：

- 不得包含`\\`
- 不得是绝对路径、带盘符的路径或 UNC 路径
- 除目录 entry 末尾用于标识目录的空段外，不得包含值为`.`、`..`或空字符串的路径段
- 不得包含 NUL 字符
- 目录 entry 必须以`/`结尾，其他 entry 必须是普通文件

ZIP 中只允许普通文件与目录，不得包含符号链接、junction、reparse point、设备、FIFO、socket 或其他特殊文件。父目录可以不作为独立 entry 出现，由安装器按普通文件的路径创建。

Package 根目录必须恰好包含一个名为`desc.json`的普通文件，名称大小写必须完全一致。其他位置的同名文件不作为 Package 描述文件。

安装器必须拒绝重复 entry、文件与目录重名，以及路径规范化后或目标文件系统名称比较规则下发生的碰撞。为保证跨平台结果一致，仅大小写不同的路径也视为碰撞。

安装器必须先将 Package 解压到同一文件系统中新建的 staging 目录。写入每个 entry 前，安装器必须将目标路径规范化，并再次确认目标位于 staging 目录内。完整解压与验证全部成功后，安装器才可以将 staging 目录原子移动到安装位置；失败时不得留下可见的半安装 Package，也不得直接在已有 Package 目录中覆盖文件。

安装器必须同时在解压前和流式解压过程中执行实现定义的资源限制，至少包括 entry 数量、单文件与总解压大小、路径长度和压缩比。不能只信任 ZIP header 中声明的大小。加密 ZIP、多卷 ZIP、CRC 校验失败、数据截断或实际大小与 header 不符时，安装器必须拒绝整个 Package。

上述路径封闭规则只适用于 ZIP 解包。声明文件中的相对资源路径仍以声明文件所在目录为基路径，允许包含`..`，规范化后可以位于 Package root 之外。模型格式内部的二级引用同样允许访问 Package root 之外的路径。定义为相对路径的字段仍不得使用绝对路径，所有访问均受宿主进程的操作系统权限约束。

> **安全边界**
>
> DSPK 不是自包含或沙箱化格式。安装器保证解包不会越界写入，但加载 Package 时可能读取 Package root 之外的文件。消费者必须将 Package 视为受信任内容。
>
> 本规范不提供 Package 来源认证，也不要求使用内容摘要或数字签名绑定`id`与`version`。这些机制由分发渠道或单独的分发规范负责。ZIP CRC 只用于检测归档损坏，不构成真实性或安全性证明。

#### 加载

由于`dspk`引入了依赖机制，因此一个`dspk`的加载流程中包含依赖查找。

- 对于一个`dspk`，当且仅当它的所有依赖项都能被加载，它才能被加载。
- 一个`dspk`的依赖项给定了所需依赖项的`id`与`version`，只有满足以下条件才能被视为合法依赖。
    - `id`与给定`id`一致
    - `version`大于等于给定`version`
    - `compatVersion`小于等于给定`version`
- 一个`dspk`将在其依赖项全部加载后进入初始化，初始化每一步都成功后即为成功加载。

`dependencies`中不得出现两个`id`相同的条目，也不得出现与当前 Package 的`id`相同的条目。引用当前 Package 中的模块不声明自依赖，使用以`:`开头的引用。

每个依赖项独立求解，不与依赖图中其他位置对相同 Package ID 的要求合并。不同依赖边可以选中同一 Package 的不同版本，因此多个版本可以同时加载。多条依赖边选中相同 ID 与相同规范化版本时，共享同一个 Package 实例。

加载器按用户给定的搜索路径顺序查找。对每个搜索路径，加载器收集该路径中所有 ID 相同且兼容依赖项所要求版本的候选。如果当前路径没有候选，则继续下一个路径；如果存在候选，则选择其中版本最高的 Package，不再搜索后续路径。同一路径中存在多个 ID 与规范化版本均相同的最高版本候选时，结果有歧义，加载失败。

依赖图必须是有向无环图。加载器在解析期间遇到自依赖或依赖环时必须拒绝加载，并报告形成环的完整依赖链。

模块的初始化分两个阶段，所有模块先各自完成第一阶段，再统一进入第二阶段：

1. **Initialized**：模块读取自己的声明文件，取得它所需的解释器或提供者。此阶段不得引用别的模块。
2. **Ready**：模块解析它对别的模块的引用。此时所有模块都已完成第一阶段，因此引用有对象可指。

任一阶段失败时，已完成的模块按**相反顺序**回滚。

> 这两个阶段只支持一层跨模块引用：第二阶段能读到别的模块第一阶段的产物，读不到别的模块第二阶段的产物。

#### 简单方案

本节将给出一种简单的安装加载方案。

安装器将所有`dspk`安装到同一个目录（如`~/.diffsinger/packages`），所有被安装的`dspk`平铺在这个目录中。
```
+ ~/.diffsinger
  + packages
      + foo
        - desc.json
        - ...
      + bar
        - desc.json
        - ...
  - ...
```

加载器可由用户在启动时指定一个路径列表（如`~/.diffsinger/packages;/opt/diffsinger/packages;./packages`）。加载器按上文规定的顺序从这些路径中解析每个依赖，直到依赖图全部解析完成。

## 2. 模块

模块的声明文件描述模块自身固有的属性。**模块 ID 不在其中**——那是 Package 赋予它的，见上文。**清单格式版本`$version`也不在其中**——那是整个 Package 共用的，写在`desc.json`里。

### 公共字段

以下字段对所有类别的模块通用，由框架统一解析。

+ 必选字段
    + `interface`：这份声明遵循哪套契约
    + `level`：该契约的 API 版本
    + `variant`：该契约下的哪一种实现变体
+ 可选字段
    + `name`：模块名称，可为多语言，如为空则与 Package 赋予它的`id`一致
    + `exports`：公开自己支持的功能集合
    + `configuration`：本模块自身的参数
    + `imports`：本模块引用的其他模块

模块声明文件的内容分三层：

| 层 | 由谁规定 | 对谁生效 | 由谁解析 |
|---|---|---|---|
| 公共字段 | 本规范 | 所有模块 | 框架 |
| 类别追加的字段 | 贡献类别 | 该类别下的所有模块，不论其契约 | 该类别自己 |
| 契约规定的内容 | `interface` + `level` + `variant` | 声明了该契约的模块 | 解释器 |

后两层的分界在**什么时候读得到它**：类别追加的字段在解释器被选出来之前就要用上（SVS 编辑器要把已安装的歌手连同头像一起列出来，那时还没决定由谁来跑它），所以由类别自己解析。`exports`按契约规定的语法读取，`configuration`则要等具体变体的解释器选定后再交给解释器。

类别追加的字段通常是可选的，某份契约用不上就不写。

#### 模块三元组

`interface`、`level`、`variant`三者构成一个**三元组**，共同回答「这份声明是什么」——与`x86_64-linux-gnu`那样的目标三元组同理：每一格都是固定的槽位，而**匹配发生在整个三元组上，不在单独某一格上**。

三格全部精确匹配。**一个 Level 就是一个 Level，不向下兼容**：声明为 Level 2 的解释器不服务 Level 1 的模块，那是另一个三元组，由另一个解释器负责。

#### 关于 `interface` 与 `variant`

模块的身份由两根正交的轴描述：

| 字段 | 回答 | 由谁定义 | 变化频率 |
|---|---|---|---|
| `interface` | **别人能对我做什么** | 本规范或第三方 | 少、稳 |
| `variant` | **谁来读我、谁来跑我** | 实现方 | 可随时增加 |

`interface`是一个反向域名形式的标识符，例如`org.openvpi.svs.AcousticInference`。它回答的是**「这份声明该按谁的语法读」**，也是导入方唯一应当据以分辨模块种类的东西。

> 2.3 中这个字段叫`class`，并被描述为「推理类型」。改名的原因是它命名的不是一个类，而是一份契约：`org.openvpi.*`是本规范定义的公共契约，第三方实现使用自己的命名空间（如`com.vendor.*`）。

`variant`区分同一契约下的不同实现变体。变体之间可以差在任何地方——模型的格式、所用的算法、乃至用不用模型（文本到音素的转换可以基于规则，也可以基于模型）。同一 (`interface`, `level`) 下的变体遵循同一套声明语法与运行时契约，但可以公开不同的能力集合，不保证彼此可互换。

某个`variant`仍然是该契约的一种实现。共享`interface`表示它们使用相同的契约词汇和调用规则，不表示每种实现必须提供契约允许的全部能力。

`variant`由`interface`定域，故本规范定义的变体用裸词即可（当前只有`onnx`一种），第三方为他人的契约提供变体时应使用反向域名（如`com.vendor.tensorrt`）。

该字段必填，**没有「默认变体」一说**。每个模块都由某个具体的实现来读取和执行，把那个实现的名字写出来，加载器才能在找不到解释器时说清楚缺的是哪一个。

加载器据`interface`、`variant`、`level`三者共同选择解释器。

#### interface 契约规范

每个 (`interface`, `level`) 必须对应一份由该 interface 所有者发布的、稳定且可定位的契约规范。官方 interface 由本规范的维护者发布，第三方 interface 由其命名空间所有者发布。同一个 (`interface`, `level`) 的契约发布后不得作不兼容修改；不兼容修改必须递增`level`或另起`interface`。

契约规范至少必须包含：

- `exports`的机器可读 JSON Schema
- `imports[].options`的机器可读 JSON Schema
- 运行时输入与输出的数据结构、数据类型、形状和单位
- 必选能力、可选能力及其语义
- 错误条件及其可观察语义

`configuration`不属于 interface 契约，其全部语法与格式版本均由`variant`规定。

#### 三个语法块的归属

`exports`、`options`、`configuration`的语法归属如下：

| | 谁读 | 是否依赖变体 | 语法由谁规定 |
|---|---|---|---|
| `exports` | 导入方 | 否 | `interface` + `level` |
| `options` | 被引用模块的解释器 | 否 | `interface` + `level` |
| `configuration` | 解释器自己 | 是 | `variant` |

由此，**同一 (`interface`, `level`) 下的所有变体共享同一套`exports`语法**。它们用同一套词汇描述自己，但内容可以不同——一个声明可以支持某项功能，另一个声明可以不支持。导入方通过引用串中的 Package、类别和模块 ID 选中一个具体模块，并读取该模块实际声明的`exports`，因此不能假定同一契约的另一个变体具有相同能力。

`configuration`的全部内容均由`variant`规定。契约不规定其中的公共字段，也不为 contract 字段保留命名空间。需要参与跨模块契约的内容应由`exports`公开，而不是作为`configuration`中的契约字段。

本规范**不为变体设立单独的版本号**。`configuration`的语法完全由该变体掌握，若需版本化，变体可以自行在`configuration`中设置格式版本字段，由解释器检查是否支持，无须本规范介入。

#### `imports`

每个条目：

+ 必选字段
    + `ref`：被引用模块的 ModuleReference，见上文引用文法。不得使用只指向 Package 的 PackageReference
+ 可选字段
    + `options`：提供给被引用模块的选项，其语法由**被引用模块**的`interface`与`level`规定

`imports`是由零至多个条目组成的有序数组。加载器必须保持声明顺序，不得排序或重排。每个条目都是独立的导入实例，相同`ref`可以重复出现，加载器不得自动合并或去重。

公共格式不提供本地 slot。导入项的用途通过被引用模块契约规定的`options`表达。导入模块需要多少项、某类导入是否必选，以及某种重复是否符合具体实现要求，由导入模块的`variant`验证。

**被引用的可以是任何模块类别中的模块，不限于 `inference`。** 类别写在`ref`里：`:inference/pitch`引用一个推理模块，`:com.vendor.language/cmn`引用一个第三方语言模块。非模块类别的贡献不能作为`imports[].ref`的目标。这也是为什么这里用一个引用串而不是拆成几个字段——拆开就没有类别的位置了。

`options`的语法**不依赖被引用模块的`variant`**。导入方按引用串选中具体模块，可以根据该模块的`exports`填写`options`；共享契约的另一个变体可以具有不同的`exports`，也就不保证能直接接受同一份导入声明。

ModuleReference 中显式出现的每个 Package ID 都必须在`desc.json`的`dependencies`中声明。引用当前 Package 时不得显式写出其 ID，而应使用以`:`开头的形式。

`ref`必须是一条完整的 ModuleReference，不设简写。2.3 中`imports`可以直接写裸字符串（如`"acoustic-1"`）表示「本 Package 的 inference 中名为 acoustic-1 的那个」，这种写法与上文的引用文法冲突——裸字符串在文法中只能归约为 PackageReference，即一个名叫`acoustic-1`的 Package。旧写法对应的完整形式是`":inference/acoustic-1"`。

### Inference 模块

Inference 模块负责执行某一项参数的推理任务，承担了最底层、核心的工作。

#### 声明文件

```json
{
    "interface": "org.openvpi.svs.VarianceInference",
    "level": 1,
    "variant": "onnx",
    "name": "Zhibin - Variance",
    "exports": {
        "predictions": [
            "breathness", "duration"
        ]
    },
    "configuration": {
        "hiddenSize": 512
    }
}
```

Inference 类别不追加任何字段。其中`exports`按契约规定的语法公开该模块支持的功能集合，`configuration`按变体规定的语法填写实现参数。

### Singer 模块

Singer 模块负责定义一个歌手的信息，以及它需要使用的其他模块。

#### 声明文件

```json
{
    "interface": "org.openvpi.synthrt",
    "level": 1,
    "variant": "diffsinger",
    "name": "Zhibin",
    "avatar": "../assets/avatar.png",
    "background": "../assets/sprite.png",
    "demoAudio": "../assets/demo.wav",
    "imports": [
        { "ref": ":inference/acoustic-1" },
        {
            "ref": "bar/pitch=1.0.0.0:inference/pitch",
            "options": { "roles": ["pitch"] }
        },
        {
            "ref": ":inference/variance-A",
            "options": { "roles": ["tension", "energy"] }
        }
    ],
    "configuration": {
        "dictionary": "../assets/dsdict.json"
    }
}
```

Singer 类别在公共字段之外追加以下可选的多语言路径字段：

+ `avatar`：头像路径
+ `background`：可用于 SVS 编辑器显示的立绘背景路径
+ `demoAudio`：可用于 SVS 编辑器预览的声音路径

这三个字段对`singer`类别下的所有契约都可用，用不上的契约不写即可。`configuration`按变体规定的语法填写歌手实现参数，`imports`按公共字段的语法列出本歌手引用的其他模块，其中每个`options`由被引用模块的契约规定。

### 关于 API Level

由于 DiffSinger 引擎架构的复杂性，我们为模块引入了 API Level 的概念，在声明文件中用`level`表示。

API Level 代表某一份`interface`的版本号，是一个正整数，每当该契约的输入或输出格式发生更新时，它将向上递增。引擎官方为每个 API Level 制定一套描述模型功能的语法。

- Inference 模块用`level`声明自己所属的 API Level，在`exports`中按该 Level 的语法公开支持的功能集合。`configuration`不属于 API Level 的语法。
- Singer 模块用`level`声明自己所属的 API Level。其`configuration`由`variant`规定。
- Singer 在每个`imports`条目的`options`中填写它选用的那部分功能，其语法由**被引用模块的**`interface`与`level`决定，而不是歌手自己的。

API Level 版本化的是**`interface`**，既不是模块，也不是变体。模块用`level`声明「我说的是这份契约的第几版」。若 Level 按变体计算，两个变体的「Level 1」便不再表示同一版契约，`exports`与`options`也无法共享词汇表。

#### 何时递增 Level

新增一样东西时，问：**导入方需要看懂它吗？**

| 情形 | 处置 |
|---|---|
| 不需要，只有解释器关心 | 写入`configuration`，**不动 Level** |
| 需要，且现有词汇表说得圆 | 用现有词汇表表达，**不动 Level** |
| 需要，但现有词汇表缺词 | **递增 Level**，为该契约补充词汇 |
| 需要，且输入输出已根本不同 | **另起一份`interface`** |

例如 Diffusion 声学模型的采样步数只有解释器关心，歌手不会因为对方是 20 步还是 50 步而改写自己的清单，故写入`configuration`，不动 Level。

这条判据的作用是抑制 Level 的增长。缺少它，每出现一种新变体便递增一次 Level，契约的词汇表将被具体变体的术语污染。

#### 按声明的 Level 读取

读取一份声明时，按它自己所声明的那一版语法去读，**那一版里没有的词，一律视为不支持**。读取方处在哪个 Level 不影响这件事，模块无须为了新增的词汇而重新发布。

这说的是导入方读取被引用模块的`exports`，与上文解释器的精确匹配是两件事：`exports`的词汇表随 Level 增长，读得懂旧词就够了。而解释器负责的是执行，一个 Level 一个解释器。

### 可扩展性

- 具有新功能的模型开发完成后，开发者为之起一个`interface`名，再基于现有的推理程序开发一个与之匹配的解释器，这样即可扩展推理功能。

- 需要为既有契约增添一种实现变体时（如在 ONNX 之外再支持别的模型格式），不必新起`interface`，只需取一个新的`variant`名并提供相应的解释器。新变体使用该契约已有的声明语法与运行时规则，但可以公开不同的能力集合；导入方改为引用新变体前，应确认其`exports`满足自身需要。

- 非 DiffSinger 甚至非 AI 的开发者，如 UTAU、Vocaloid，亦可通过扩展`interface`来支持其他引擎，可以使用混合 Package 将歌手信息与歌声采样放在同一个 Package 中。

- 需要一种全新的模块（而不只是一种新的推理）时，扩展**贡献类别**：链接进宿主的库注册一个新类别，此后`desc.json`就可以在`contributes`下列出它，引用文法也自动支持它。本规范不限制类别的集合。

### 推荐目录结构

```
+ somedspk
  + assets
    - avatar.png
    - dict.json
  + inferences
    + acoustic
      - inference.json
      - acoustic.onnx
    + duration
      - inference.json
      - duration.onnx
    - ...
  + singers
    + singer1
      - singer.json
  - desc.json
```

- 共享的资源文件放置在`assets`中
- 推理模块放置在`inferences`的子目录中，每个子目录一个声明文件，歌手模块同理
- 根目录固定放置`desc.json`

## 3. 推理插件开发

在`plugins`中添加插件。

### 推理解释器

插件必须在插件元数据中列出它提供的推理解释器。每个解释器条目包含：

- `interface`：负责的契约，如`org.openvpi.svs.PitchInference`
- `variant`：负责的变体
- `apiLevel`：负责的那一个 API Level

插件元数据必须能在不加载插件的情况下读取。加载器按插件框架定义的插件搜索顺序扫描元数据，选择第一个与模块的`interface`、`variant`和`level`全部匹配的解释器条目。后续出现的相同三元组不参与选择。

选中条目后，加载器才加载对应插件并创建派生于`InferenceInterpreter`的解释器。解释器的`create`创建对应的推理任务类。

### 推理任务

创建派生于`Inference`的推理任务类。

- `initialize`：初始化推理任务，应当加载需要用到的模型
- `start`：开始推理任务，应当对输入的参数进行预处理，并构建推理图
- `startAsync`：`start`的异步版本
- `stop`：立即停止推理任务（同步）
- `state`：推理任务状态
- `result`：推理结果
