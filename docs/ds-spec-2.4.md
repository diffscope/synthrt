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
| 运行时要求 | 无 | `desc.json`必须用`runtimeLevel`声明本规范统一分配的最低运行时能力等级，当前为 1 |
| JSON profile | 未规定 | UTF-8，允许注释，拒绝重复 key 与尾随逗号 |
| 结构标识符 | Package ID 仅列出禁用字符，其他标识符未统一 | 仅限 ASCII，区分大小写并逐字节精确比较 |
| Package 版本 | `x.y[.z.w]` | 一至四个无前导零的十进制分量，按任意精度整数比较并在右侧补零 |
| 字符串变量 | 未规定 | `desc.json`和模块声明可定义非多语言字符串变量，并按声明顺序展开；`${root}`与`${dir}`可用于锚定路径 |
| 声明路径 | 未规定 | `/`与反斜杠均作为分隔符；允许绝对路径，相对路径按字段所在声明文件定基 |
| 模块 ID | 写在模块自己的声明文件里 | **写在 `desc.json` 里，由 Package 赋予** |
| `class` | 推理类型 + 解释器选择，一词两义 | 改名 `interface`，只表示「这份声明遵循哪套契约」 |
| 实现变体 | 无，只能另起一个 `class` | 新增 `variant`，表示同一契约的不同实现；不同变体不保证可互换 |
| 术语 | Library（亦称 Package） | 统一用 **Package**，不再用 Library / lib |
| `$version` | 每份模块声明文件各写一份，`desc.json` 反而没有 | **只写在 `desc.json`**，全 Package 共用一个 |
| 模块声明文件的公共字段 | 各类别各自定义、各自解析 | 提取为《公共字段》，由框架统一解析 |
| `schema` | 名为 schema，装的却是功能清单 | 改名 `exports`，与 `imports` 成对 |
| `configuration`的语法 | 未区分契约与实现 | 完全由`variant`规定，契约不规定其中的公共字段 |
| 多语言值 | Runtime 按语言代码选择文字 | Runtime 忠实提供完整 map，不解释语言代码；路径 value 在返回前完成解析与规范化 |
| 多语言路径 | 未明确落到具体字段 | `readme`、`avatar`、`background`与`demoAudio`均为多语言路径 |
| `imports` 条目 | `id` + `inferenceId` + `version` 三个字段，或一个裸字符串简写 | 一个 `ref` 引用串，**类别是其中一格**，不设简写 |
| `imports`集合语义 | 未规定 | 有序数组，重复引用是独立导入实例，不自动去重 |
| 模块引用 | `lib[version]/id`，Package、版本与模块混在一起 | ModuleReference 只定位已解析依赖中的模块，版本要求只写在`dependencies`中 |
| 可选依赖 | `dependencies[].required`，默认 `true` | **不支持**，所有依赖均为强制依赖 |
| 依赖求解 | 候选选择与依赖图约束未规定 | 路径优先，同一路径选择最高兼容版本；允许多版本共存，禁止重复依赖、自依赖与环 |
| Package 身份 | ID 与版本的实例共享语义未规定 | `(id, normalized-version)`唯一标识 Package，靠前搜索路径的来源遮蔽后续同身份来源 |
| Package 加载来源 | DSPK 与安装目录的职责未分 | `.dspk`只由 Installer 安装，Loader 只读取已安装 Package 目录 |
| 清单解析模式 | 未规定 | `Probe`只读取和验证清单并选择 provider，`load`在成功完成`Probe`后启动解释器；依赖求解不回溯 |
| 加载事务与生命周期 | Initialized / Ready 后反序回滚 | DLL 风格强引用计数，Commit 前只允许同步管理调用并禁止业务执行，Commit 不可失败；卸载执行 quit / wait，Runtime 销毁前静止 provider domain |
| Package 兼容承诺 | `compatVersion`只参与版本区间判断 | 明确兼容区间内必须保持的公开表面，由 Package 作者负责 |
| DSPK 安全边界 | 未规定 | ZIP entry 文件名使用 UTF-8，解包严格封闭在安装目录内；运行时资源引用允许越过 Package root |
| 模块 provider 发现 | 只规定 Inference 解释器 | 每个模块类别有专用插件搜索目录与 factory，无效元数据跳过，目录内按文件名确定排序，首次创建时选择并永久绑定首个 provider |
| interface 契约 | 只有名称与 API Level | 每个 (`interface`, `level`) 必须有稳定可定位的规范及 JSON Schema |
| 契约发现 | 未规定 | 契约材料面向开发者发布，Runtime 不按 interface 自动发现或下载 Schema |
| category 注册 | 只有开放类别概念 | 解析前完成进程内注册，重复名称报错，Package 不能定义 category 语义 |
| Inference Task API | 单独规定执行方法 | 移出本规范，由运行时 API 规范定义 |
| 命令行工具 | 单独一章 | 移出本规范 |

---

## 1. 关于 Package（旧称 Library）

### 文件结构

本规范内，可分发的数据包的最小单位是 Package（包），是一个以`dspk`为扩展名的 ZIP 格式的单文件压缩包。一个 DSPK 不得依赖辅助卷或其他分卷文件才能读取。

压缩包内基本结构为：
```
+ xxx.dspk
  - desc.json
  - ...
```

Package 内多使用`json`作为声明文件。声明文件中使用的相对路径以该声明文件所在目录为基路径。

#### JSON profile

本规范中的所有 JSON 声明文件必须使用 UTF-8。允许 UTF-8 BOM，并在解析前忽略。声明文件允许`//`行注释与`/* */`块注释，不允许尾随逗号。同一个 object 的同一层中不得出现重复 key，违反时整份声明无效。注释标记只在 JSON 字符串之外识别，字符串内的相同字符仍是字符串内容。

除上述注释扩展外，JSON 语法遵循 RFC 8259。块注释是否嵌套、行注释采用哪些行终止符及其他注释词法边界由实现使用的 JSON 解析器决定，本规范不指定固定 JSONC grammar；依赖这些边界差异的声明不保证跨实现兼容。`null`只在对应 schema 明确允许时合法。框架定义的 object 遇到未知字段时拒绝，贡献类别、interface 与 variant 定义的 object 按各自 schema 处理。实现可以设置 JSON 字节数、嵌套深度、节点数与字符串长度等资源限制，超过限制时拒绝声明。

#### 声明路径

声明中的路径字符串使用 UTF-8，`/`与 U+005C REVERSE SOLIDUS（反斜杠）均视为路径分隔符。加载器必须先按此规则拆分并规范化路径，再交给宿主文件系统处理，因此路径分隔语义不随操作系统改变。

声明中的资源路径不得包含 NUL，但可以是绝对路径，也可以是相对路径。完成字符串变量展开后，绝对路径直接使用；相对路径以该字段所在声明文件的目录为基准解析。`.`与`..`路径段允许出现，规范化后的路径可以位于 Package root 之外。盘符、UNC、device 前缀及其他宿主路径形式由宿主文件系统解释。

##### 字符串变量

`desc.json`和每个模块声明文件都可以在根 object 中使用可选的`vars`字段定义字符串变量。`vars`必须是 array，每项必须是只包含字符串字段`name`和`value`的 object。变量值只能是普通字符串，不得是多语言对象。除各层级的`$version`和`vars[].name`外，变量可以在声明文件中的 JSON 字符串值内使用；多语言对象的每个 value 分别使用同一组非多语言变量展开，Runtime 保存完整 map，不从中选择语言。`$version`和`vars[].name`必须按未经展开的原始值验证，二者不得包含变量引用。

变量引用写作`${name}`，可以单独作为整个字符串，也可以与其他文本组合，例如`${assets}/singer1/avatar.png`。变量引用允许嵌套并从最内层开始展开，例如`${${selector}}`先展开`${selector}`，再将其结果作为外层引用的变量名。字符串中的`$$`表示一个字面的`$`；除此之外，原始字符串中出现的`${`必须具有配对的`}`。引用 payload 在完成其中显式嵌套引用的求值后作为变量名查找；结果为空、不匹配`var-name`或找不到同名变量时均展开为空字符串。因此`${}`直接展开为空，`${${var}}`在`${var}`得到`bad-name`时也展开为空。

每个原始 JSON 字符串只进行一次 tokenization 和求值。变量替换得到的字符串不得再次作为模板扫描，只有原始字符串中显式写出的嵌套引用参与由内到外的求值。例如变量`d`的值为`$`时，`${d}{target}`和`$${target}`都展开为字面字符串`${target}`，不得继续展开`target`。整个变量展开必须由 Loader 在把声明交付给后续阶段之前统一完成，framework、category、interface 或 variant 不得对已展开的字符串再次应用本节规则。

字符串变量只携带字符串内容，不携带其定义位置或路径基准。用于路径字段时，应先完成整个字段的变量展开；展开结果为绝对路径时直接使用，为相对路径时再以该字段所在声明文件的目录为基准解析，随后处理路径分隔符、`.`与`..`及规范化。`${root}`和`${dir}`是 Runtime 提供的普通字符串变量，其值可以像其他字符串一样经由自定义变量继续传播。

Runtime 提供两个保留变量：

- `${root}`：Package root，即`desc.json`所在目录
- `${dir}`：当前声明文件所在目录；在`desc.json`中与`${root}`相同

Runtime 为`${root}`和`${dir}`生成的值必须是 fully-qualified、词法规范化且能由本规范路径字符串无损表示的路径。Package root 或声明文件目录无法满足该要求时，Loader 必须拒绝该 Package 来源。

`root`与`dir`不得在`vars`中重定义。自定义变量名必须匹配`var-name = (ALPHA / "_") *(ALPHA / DIGIT / "_")`，只允许 ASCII 并区分大小写。

`desc.json.vars`定义 Package 变量，对该 Package 的`desc.json`及全部模块声明可见。模块声明中的`vars`定义 Module 变量，只对当前模块声明可见。Module 变量查找时先查当前模块，再查 Package，可以覆盖同名 Package 变量，但不会影响其他模块。Package 变量自身始终在 Package 作用域解析，不因 Module 的同名覆盖而重新绑定。这里的作用域只决定变量查找，不赋予变量值路径基准。

`vars`按数组顺序求值。每一项只能找到外层作用域变量和本数组中位于它之前的变量，其他引用按找不到变量处理并展开为空字符串，因此不会形成变量循环。同一`vars`数组内不得出现重复变量名。

本规范不规定解析完成后的内存对象是否保留`vars`及其求值过程，但所有交付给后续阶段的字符串都必须已经完成变量展开，可以直接使用，不得要求使用者再次解析变量。推荐实现在构造最终内存对象后移除`vars`，只保留展开后的字符串。

模型格式或其他资源文件内部的二级引用不会自动继承 Package 或 Module 变量，除非相应格式另有规定。

#### 描述文件

`desc.json`是 Package 的描述文件，主要包括以下内容。

```json
{
    "$version": "1.0",
    "id": "foo",
    "version": "1.0.0.0",
    "compatVersion": "0.0.0.0",
    "runtimeLevel": 1,
    "vars": [
        { "name": "assets", "value": "${root}/assets" }
    ],
    "vendor": "someone",
    "copyright": "Copyright (C) someone",
    "description": "Some package",
    "readme": "${assets}/readme.txt",
    "url": "https://www.example.com",
    "contributes": {
        "inference": [
            { "id": "acoustic", "path": "./inferences/acoustic/inference.json" },
            { "id": "variance", "path": "./inferences/variance/inference.json" }
        ],
        "singer": [
            { "id": "zhibin", "path": "./singers/singer1/singer.json" }
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
    + `runtimeLevel`：加载该 Package 所需的最低运行时能力等级，必须是正整数，见下文《Runtime Level》
+ 可选字段
    + `compatVersion`：兼容到的最低版本，如果为`0.0.0.0`表示向下兼容所有，如果与`version`相同则表示不向下兼容，缺省为`version`
    + `vars`：Package 级字符串变量，见上文《字符串变量》
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

加载器应在解析`contributes`之前先检查`$version`与`runtimeLevel`。遇到不认识的清单格式版本，或当前运行时支持的 Level 低于`runtimeLevel`时，应当整包拒绝，而不是继续解析后面的字段。这也正是它们必须写在`desc.json`而非各模块声明文件里的原因——`desc.json`是最先被读到的文件。写在模块声明文件里的版本号，要等到整个 Package 已被接受、依赖已被解析之后才轮得到检查，那时拒绝已经太晚。

`dependencies`中的每个 Package 各自携带自己的`$version`，在其被加载时独立检查，不要求与本 Package 一致。

#### 版本

版本号由一至四个以`.`分隔的十进制非负整数组成。数字分量采用 Semantic Versioning 的数值标识符原则：单独的`0`合法，其他分量必须以`1`至`9`开头，不允许前导零。

```
version   = component *3("." component)
component = "0" / nonzero-digit *DIGIT
nonzero-digit = %x31-39
```

每个分量没有固定数值上限，实现必须按任意精度非负整数比较，不得因超出机器整数范围而改变比较结果。JSON profile 的整体字符串长度等资源限制仍然适用。

比较前在右侧补`0`至四段，各段按整数比较。`1`、`1.0`、`1.0.0`与`1.0.0.0`因此表示同一版本。需要序列化规范化版本时必须输出补齐后的四个分量，且每个分量不含前导零。

Package 的身份由`id`与规范化后的`version`共同确定。发布者必须保证使用同一身份发布的 Package 内容相同，包括`desc.json`、Package root 内的全部文件，以及声明直接或间接引用的 Package root 外部文件及其相对布局。发布者还必须保证这些文件在 Package 的解析、加载和使用期间不发生影响其布局、内容或语义的变化。违反上述要求属于 Package 缺陷，其运行时行为未定义；加载器不负责读取多个来源并比较或证明其内容一致，也不负责监视或诊断文件变化。

Package 的`compatVersion`不得大于`version`，否则清单无效。一个版本为`version`、最低兼容版本为`compatVersion`的 Package 兼容目标版本`target`，当且仅当：

```
compatVersion <= target <= version
```

声明兼容某个目标版本，表示在宿主满足当前 Package 所有运行时要求的前提下，当前 Package 可以在数据格式与 API 契约层面替换该目标版本，而不要求依赖方修改声明。对兼容区间内已经发布的同 ID Package，当前 Package 必须保持以下公开表面：

- 已有贡献的类别、模块 ID 及其含义
- 已有模块的`interface`与`level`
- 已公开的`exports`能力及其语义
- 既有`options`写法及其语义
- 契约规定的运行时输入输出、单位、错误语义与资源所有权
- 贡献类别规定的必选字段及其兼容的数据类型和语义

当前 Package 可以增加贡献和可选能力，也可以修改不影响上述公开表面的实现细节、私有`configuration`、模型与算法。`variant`可以改变，但改变后仍须履行原有公开能力与运行时契约。任何破坏上述承诺的版本都必须将`compatVersion`提高到不再覆盖受影响的旧版本。

兼容性是 Package 作者作出的承诺。加载器只按版本区间判断候选，不负责读取或比较历史版本来证明该承诺。该承诺不保证当前宿主满足新版本要求的`runtimeLevel`、贡献类别、解释器、外部资源或其他运行环境条件。承诺不实属于 Package 缺陷，加载器或执行器在缺陷实际导致失败时应报告诊断。

#### Runtime Level

Runtime Level 是由本规范维护者统一分配的正整数，表示 Package 可以依赖的核心运行时能力，不是 SynthRT 或其他产品的软件版本。每个兼容运行时声明自己支持的最高 Level，Package 的`runtimeLevel`不得高于该值。

Level 是对某一作用域内能力版本的统称。`runtimeLevel`序列化 Package 要求的 Runtime Level，模块及 provider metadata 中的`level`序列化对应 (`interface`, `level`) 契约的 API Level；二者分别编号，不属于同一数值空间。

当前规范定义 Runtime Level 1，保证 Package 与 dependency、ModuleReference、`inference`与`singer`内置类别，以及模块 provider 发现机制。新增 Package 可以依赖的核心能力或内置类别时递增 Level；修复实现缺陷、增加 provider 或注册第三方类别时不递增。

`runtimeLevel`是`desc.json`的必选字段，没有缺省值。当前版本的本规范只分配了 Level 1，因此当前的 conforming Package 必须写`"runtimeLevel": 1`。字段缺失、不是正整数或高于当前运行时支持的最高 Level 时，加载器必须拒绝整个 Package。

`$version`负责声明文件格式，`runtimeLevel`负责运行时核心能力。第三方类别不由 Runtime Level 保证，仍然必须在宿主中实际注册。

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

每个 (`Package`, category, contribution ID) 唯一确定一个 contribution instance。对于模块类别，它也唯一确定一个 module instance；不同 ID 即使在变量展开和路径规范化后指向同一个声明文件，也必须创建彼此独立的实例，不得按路径自动合并或推导别名。

贡献不一定是模块。类别注册时必须声明其条目是否指向模块声明。模块类别的条目必须使用`path`指向包含公共模块字段的声明文件，并注册该类别专用的 provider factory 与插件搜索目录；`inference`与`singer`都是模块类别。非模块类别的条目不使用公共模块字段和模块 provider，其内容完全由类别规定，可以内联，也可以按该类别自己的规则引用其他文件。

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

所有 category 必须在解析任何 Package 前完成进程内注册。一次注册至少包含 category 名称、条目是模块还是非模块，以及该类别的条目解析器或 schema。模块类别还必须注册专用 provider factory 和有序插件搜索路径，非模块类别则注册自己的条目解析器，不使用模块 provider。

同一个 category 名称在一个进程中只能注册一次，重复注册必须报错，不得按注册顺序覆盖或合并。Package 只能使用已经注册的 category，不能自行定义、替换或扩展 category 的语义。category 注册来自宿主及其链接或安装的扩展，不从 Package 获取。

加载器遇到不认识的类别时必须拒绝整个 Package。`runtimeLevel`可以防止旧运行时尝试加载依赖较新内置类别的 Package，但不能代替第三方类别的注册；声明了第三方类别的 Package 仍要求当前宿主已经注册该类别。

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

+ `_`**必选**，表示由内容作者提供的默认值
+ 其余属性名是由内容作者和前端约定的语言代码，例如`en`、`zh-CN`、`zh-Hant-TW`、`ja-JP`

只写一个字符串，等价于只有`_`一项的对象。

Runtime 不解释这些属性名，不验证 BCP 47、POSIX locale 或其他语言代码语法，不执行大小写折叠、canonicalization、fallback 或 Lookup，也不接收语言偏好来代替前端选择。除 JSON 本身禁止重复 key 外，语言代码在 Runtime 中是不透明且区分大小写的 map key；例如`zh-CN`与`ZH-cn`是两个不同的 key。Runtime API 必须允许前端取得完整 map，并忠实返回原样的 key。多语言文本的 value 是完成变量展开后的字符串，多语言路径的 value 按下文规则返回。

推荐使用 BCP 47 语言标签，但不作强制要求；内容作者只需与消费该 map 的前端约定一致。采用哪套语言代码、如何匹配用户偏好、何时使用`_`以及是否合并大小写不同的 key，均由前端决定。

#### 用在哪些字段

标准字段的类型如下：

| 字段 | 类型 |
|---|---|
| `vendor`、`copyright`、`description` | 多语言文本 |
| `name` | 多语言文本 |
| `readme` | 多语言路径 |
| `avatar`、`background`、`demoAudio` | 多语言路径 |

贡献类别与契约追加的字段是否多语言，以及是文本还是路径，由定义它们的一方明确规定。

某个多语言字段的值是文件路径时，**每一个本地化的值各自解析**，基路径都是该声明文件所在的目录。Runtime API 向前端提供的多语言路径 map 必须包含完成变量展开、基路径解析和词法规范化后的路径 value，不得只返回脱离声明目录上下文的原相对字符串。具体宿主路径类型由 Runtime API 规定。

### 结构标识符与 ModuleReference 文法

Package ID、category、contribution ID、`interface`与`variant`统称结构标识符。结构标识符只允许下述文法中的 ASCII 字符，区分大小写，并按 UTF-8 中等同的 ASCII 字节序列精确比较；不得执行 Unicode normalization、locale 相关转换或大小写折叠。多语言 map 的 key 不是结构标识符，按上文规定作为不透明字符串保存。

ModuleReference 指向一个模块。外部 ModuleReference 以 Package ID 绑定当前 Package 已经解析出的直接依赖，再用`:category/contrib-id`定位其中的模块；引用当前 Package 的模块时省略 Package ID，但必须保留开头的`:`。

```
module-reference  = package-id ":" category "/" contrib-id
                  / ":" category "/" contrib-id

package-id    = segment *( "/" segment )
category      = dotted-id
contrib-id    = segment
interface     = dotted-id
variant       = dotted-id
dotted-id     = segment *( "." segment )
segment       = 1*( ALPHA / DIGIT / "_" / "-" )
```

```
vendor/sample:inference/pitch    // 已解析的外部直接依赖
vendor/sample:singer/main
:singer/main                     // 当前 Package
```

+ ModuleReference 中没有 Package ID 时，前导的`:`不可省略。
+ ModuleReference 必须包含 category。声明文件中不得使用省略 category 的持久化引用。
+ **类别是文法中的一格数据，而不是标点。** 新增一种贡献类别不需要改动这条文法。
+ ModuleReference 不携带 Package 版本。外部 ModuleReference 始终绑定`dependencies`求解选定的 Package 实例，不得触发新的 Package 搜索或加载另一个版本。
+ 引用当前 Package 的模块必须使用以`:`开头的形式。显式写出的 Package ID 始终表示外部 Package，并且必须在`dependencies`中声明。

### 安装与加载

#### 安装

对于`dspk`文件，安装就是在某个目录中解压它。本规范对这个目录没有任何要求。`.dspk`只作为 Installer 的输入；Loader 只从已经完成安装的 Package 目录执行`Probe`与`load`，不得直接把 raw DSPK 作为候选或临时物化后加载。

##### 安全解包

ZIP entry 文件名必须使用 UTF-8，安装器不得按 CP437、系统代码页或 locale 解码文件名。无法得到有效 UTF-8 文件名的 entry 使整个 Package 无效。

ZIP 中的 entry 路径必须使用`/`作为分隔符，并满足以下要求：

- 不得包含 U+005C REVERSE SOLIDUS（反斜杠）
- 不得是绝对路径、带盘符的路径或 UNC 路径
- 除目录 entry 末尾用于标识目录的空段外，不得包含值为`.`、`..`或空字符串的路径段
- 不得包含 NUL 字符
- 目录 entry 必须以`/`结尾，其他 entry 必须是普通文件

ZIP 中只允许普通文件与目录，不得包含符号链接、junction、reparse point、设备、FIFO、socket 或其他特殊文件。父目录可以不作为独立 entry 出现，由安装器按普通文件的路径创建。

Package 根目录必须恰好包含一个名为`desc.json`的普通文件，名称大小写必须完全一致。其他位置的同名文件不作为 Package 描述文件。

安装器必须拒绝重复 entry、文件与目录重名，以及路径规范化后或目标文件系统名称比较规则下发生的碰撞。为保证跨平台结果一致，仅大小写不同的路径也视为碰撞。

安装器必须先将 Package 解压到同一文件系统中新建的 staging 目录。写入每个 entry 前，安装器必须将目标路径规范化，并再次确认目标位于 staging 目录内。完整解压与验证全部成功后，安装器才可以将 staging 目录原子移动到安装位置；失败时不得留下可见的半安装 Package，也不得直接在已有 Package 目录中覆盖文件。

安装器必须同时在解压前和流式解压过程中执行实现定义的资源限制，至少包括 entry 数量、单文件与总解压大小、路径长度和压缩比。资源限制必须依据 ZIP 实现最终产生的实际 entry 与数据量执行，不得仅依据归档元数据中的声明值。

除 UTF-8 文件名、单文件分发和上述安全后置条件外，本规范不定义 DSPK 专用 ZIP feature profile，也不解释 ZIP 内部元数据。实现可以自行决定支持的 compression method、ZIP64、data descriptor、Unicode extra field、加密、CRC 及其他不依赖辅助卷的 ZIP 特性，并完全负责 header、central directory、extra field 与 descriptor 的解析、一致性和权威值选择；不支持或判定无效时可以拒绝整个 Package，不要求不同安装器接受完全相同的 ZIP 功能集合。无论使用哪些 ZIP 特性，成功安装的结果都必须满足本节全部 UTF-8 文件名、路径、entry 类型、资源限制和原子安装规则。

上述路径封闭规则只适用于 ZIP 解包。声明文件中的资源路径可以是绝对路径；相对路径以声明文件所在目录为基路径，允许包含`..`，规范化后可以位于 Package root 之外。模型格式内部的二级引用同样允许访问 Package root 之外的路径。所有访问均受宿主进程的操作系统权限约束。

> **安全边界**
>
> DSPK 不是自包含或沙箱化格式。安装器保证解包不会越界写入，但加载 Package 时可能读取 Package root 之外的文件。消费者必须将 Package 视为受信任内容。
>
> 本规范不提供 Package 来源认证，也不要求使用内容摘要或数字签名绑定`id`与`version`。这些机制由分发渠道或单独的分发规范负责。ZIP 实现提供的完整性检查不构成来源真实性或安全性证明。

#### 加载

由于`dspk`引入了依赖机制，因此一个`dspk`的加载流程中包含依赖查找。

清单解析器提供两种模式：

- `Probe`：读取依赖图中的`desc.json`和模块声明文件，验证框架公共 envelope 与 category 条目基础结构，确认 category 已注册，解析 ModuleReference，并从插件元数据中选择 provider。此模式不得加载插件、实例化解释器或取得模型、设备等运行时资源。
- `load`：在成功完成`Probe`后加载插件、实例化解释器并取得运行时资源。

本规范不定义可持久化或不可变的 LoadPlan，也不要求`load`复用`Probe`读取的文件快照。实现可以缓存解析结果，也可以重新读取或解析 Package 及其 root 外部引用；只要 Package 作者履行同身份内容与使用期间稳定的承诺，两种实现的输入就应等价。实现不必检测变化或报告`StalePlan`。

`Probe`不根据 interface 名称发现或下载契约 Schema，也不验证具体契约。`exports`、`options`与`configuration`在此阶段只读取为 JSON，具体契约语义只能由选中的 provider 在`load`阶段验证。

依赖求解始终先使用`Probe`读取候选的`desc.json`。一个 Package 只有同时满足以下条件才是某个 dependency edge 的候选：

- 安装目录能够按本规范读取
- `desc.json`是有效清单
- `$version`受当前加载器支持
- `id`与 dependency 要求一致
- `version`与`compatVersion`合法，并且兼容 dependency 要求的目标版本
- `runtimeLevel`不高于当前运行时支持的 Level
- `contributes`中列出的所有贡献类别均已注册

加载器对上述候选按搜索路径优先、同一路径选择最高版本的规则作出一次选择。选中后，不论后续递归依赖解析、其余清单的`Probe`验证、provider 发现、`configuration`检查、解释器启动或模块初始化在哪一步失败，整条加载都必须失败，不得回退到其他候选版本，也不得重新搜索其他路径。

对于一个已安装 Package，当且仅当它的所有依赖项都能被加载，它才能被加载。dependency 要求的目标版本必须位于候选 Package 的闭区间`[compatVersion, version]`内。

`dependencies`中不得出现两个`id`相同的条目，也不得出现与当前 Package 的`id`相同的条目。引用当前 Package 中的模块不声明自依赖，使用以`:`开头的引用。

每个依赖项独立求解，不与依赖图中其他位置对相同 Package ID 的要求合并。不同依赖边可以选中同一 Package 的不同版本，因此多个版本可以同时加载。多条依赖边选中相同 ID 与相同规范化版本时，必须共享同一个 Package 实例。

加载器按用户给定的搜索路径顺序发现 Package。一个搜索路径中存在多个 ID 与规范化版本均相同的 Package 时，结果有歧义，加载失败。某个 Package 身份第一次出现时，其所在来源成为该身份在本次解析中的唯一来源；后续搜索路径中出现的同身份 Package 被遮蔽，不再参与任何 dependency edge 的候选选择。

对每个 dependency edge，加载器在上述唯一来源集合中按静态条件收集候选，并继续遵守搜索路径优先规则。如果当前路径没有候选，则继续下一个路径；如果存在候选，则选择其中版本最高的 Package，不再搜索后续路径。所有选中同一 Package 身份的 dependency edge 绑定由首次出现来源提供的同一个实例。相对路径均以该实际来源为基础解析。

依赖图必须是有向无环图。加载器在解析期间遇到自依赖或依赖环时必须拒绝加载，并报告形成环的完整依赖链。

##### Package 实例与强引用

加载器维护按 Package 身份索引的实例表。每个实例具有强引用计数，强引用来自：

- 调用方持有的根 Package load handle
- 已 Commit Package 的 dependency edge
- 尚未 Commit 的加载事务临时引用

同一个 Package 实例被再次作为根 Package 加载时，不重复初始化，只增加一个外部 handle 引用。一个已 Commit Package 对每个直接 dependency 持有一个强引用；ModuleReference 本身不另行计数，因为其目标已由当前 Package 或直接 dependency edge 保活。每个 module instance 还记录首次创建它的实际 provider 及 provider plugin，该绑定在实例的整个生命周期中不可改变。

##### 加载事务

同一个 Loader 上改变 Package 实例状态的 load 与 release 事务必须串行执行。一次 load 事务覆盖请求的根 Package，以及本次依赖闭包中尚未 Commit 的 Package 实例。事务依次执行：

1. **Probe**：无副作用地完成依赖求解、清单解析与 ModuleReference 绑定。对于本事务将要创建的新 module instance，执行 provider 元数据选择；对于已经 Commit 的现存实例，直接使用实例记录的 provider 绑定，不重新发现或选择。
2. **Acquire**：为根 handle 和每条 dependency edge 取得临时强引用，将尚未加载的 provider plugin 加入 Runtime 常驻插件集合，并创建本次所需的新 Package 与模块实例。每个模块的 provider 必须同步验证该模块自身的声明、`exports`与`configuration`，验证成功后才完成该模块的 Acquire 并取得尚未激活的运行时资源。已经 Commit 的实例只增加临时引用，不重复 Acquire。
3. **Ready**：所有新模块 Acquire 成功后，被引用目标的 provider 必须同步验证每个 import 条目的`options`，importing module 的 provider 必须验证有序 imports 集合是否满足自身对数量、顺序、目标 interface 与 level、能力及组合关系的要求；随后根据`Probe`阶段解析的 ModuleReference 为每个条目创建事务私有且尚未激活的 ImportBinding。Ready 可以读取已 Commit 实例和其他模块的 Acquire 产物，不得依赖其他新模块的 Ready 产物，也不得使未 Commit 的 binding 或连接状态对任何运行时读取者可见。
4. **Commit**：全部 Ready 成功后，以不可失败的操作原子发布本次新建的 Package 实例及其 ImportBinding，并将其运行时外部入口从关闭状态切换为`Running`。根 Package 的临时引用转交给返回的 load handle，各 dependency 临时引用转交给已 Commit 的 dependency edge。

Commit 前，本次新建的实例及其 ImportBinding 不得对其他 load 调用或运行时读取者可见。Commit 后，调用方得到一个持有根 Package 强引用的 handle。

加载期间的调用分为两类：

- **管理调用**：由 Loader 为当前事务发起的声明、契约、`exports`、`configuration`、`options`或 imports 集合验证，以及建立连接所需的 prepare / commit / abort 生命周期调用。管理调用可以在 Acquire、Ready、Commit 或 rollback 的相应阶段进入本事务的新实例或已 Commit 的目标实例，但必须同步完成并返回，不得启动线程、异步任务或其他自主执行活动。除本节明确规定为不可失败的操作外，管理调用失败按所在阶段的加载失败或 rollback 规则处理；其状态变化必须满足相应的事务私有性与完成日志要求。
- **运行时调用**：由 Package、模块、ImportBinding、外部运行时使用者或 callback 发起的合成、推理、音频处理及其他业务执行调用。

Commit 是新建 Package 实例从不可执行状态进入可执行状态的唯一边界。Commit 前，新实例、模块、运行时资源与 ImportBinding 均不得接收运行时调用，不得启动线程或异步任务，不得注册可从事务外触发的 callback，也不得通过已 Commit 实例执行任何运行时调用。Loader 可以按本节规则对已 Commit 实例执行同步管理调用。provider 的验证和准备操作必须在调用返回前完成，不得遗留任何自主执行活动。违反本段要求属于 provider 缺陷。

ImportBinding 的激活状态与 owning Package 一致：Ready 创建的 binding 保持关闭，Commit 时随 Package 原子打开，允许 importer 与 target 通过它双向调用；owning Package 进入`Stopping`时，必须先原子关闭 binding 双方发起新调用和 callback 的入口。未 Commit 且从未激活的 binding 可以在 rollback 中直接销毁。

provider 必须保证每项运行时活动在任一时刻恰好归属于一个具体 Package 实例或一个已注册的 provider execution domain。由 Package 发起的活动初始归属于该 Package，其中通过 ImportBinding 产生的活动还必须归属于具体 binding。provider 必须提供 ImportBinding 及其调用级的停止隔离。关闭一条 binding 后，provider 必须能够停止、取消或隔离归属于该 binding 的全部既有活动，使 owning Package 的 wait 可以在不永久破坏共享 target 上其他仍有效 binding 与活动的前提下完成。在确认这些活动不可能再访问 owning Package 或该 binding 前，不得销毁二者。

隔离允许底层计算继续运行，但必须原子结束它对原 Package 与 binding 的归属，并将 continuation 重新归属于 target 或 provider 执行域。continuation 必须独立持有继续运行所需的资源，不得再访问原 Package、binding 或原 Package 即将释放的 dependency edge；完成重新归属后，它不再属于原 Package 的 wait 集合。provider 级 scheduler、heartbeat 及其他执行域基础设施不归属于任一 Package，但不得在 Package 或 binding 归属结束后继续保存其指针或借用资源。provider 可以通过取消、迁移、重建、恢复或其他机制满足该结果，本规范不规定具体机制。不能满足该要求的 provider 不得让生命周期相互独立的 ImportBinding 共享同一个不可分割的故障单元；违反本要求的 provider 不合法，后续行为未定义。

Package 中每个模块都必须完成上述 provider 验证，不论它是否被其他模块 import。任何声明、`exports`、`configuration`、`options`或 imports 集合验证失败，都必须使整次加载失败并 rollback；加载器不得 Commit 含有未验证模块或契约错误的 Package，也不得回退到其他 provider。本规范不增加独立的 Validate 阶段。

所有可能失败、分配资源、执行 I/O 或产生需要撤销的状态变化的工作都必须在 Acquire 或 Ready 完成。验证操作不得留下未记录的持久副作用；Acquire 或 Ready 创建的每项资源和状态变化都必须保持事务私有且立即写入完成日志，以便 rollback 完整撤销。

Ready 不得通过修改已 Commit 目标模块的公开全局状态来建立连接。如果 provider 必须修改共享目标，其接口必须提供等价于 prepare / commit / abort 的事务机制，或使用覆盖运行时读取者的同步机制。prepare 在 Ready 中完成全部可能失败的工作，其产物保持事务私有并写入完成日志。abort 的逻辑撤销必须不可失败，必须先从共享目标中移除本事务产生的 binding、registration、指针及其他可访问或可遍历状态，保证共享目标不再引用将被销毁的事务对象。随后进行的文件、设备或其他物理资源回收可以失败并作为附加诊断报告，但不得恢复已经解除的逻辑关联。commit hook 必须不可失败且不得分配资源或执行 I/O，只能在与 Package 实例表发布相同的可见性屏障中公开已经准备完成的状态。

Commit 一旦开始就必须完整结束，不得返回错误或转入 rollback。Commit 只能执行状态切换、指针交换或等价的内存发布操作；任何无法满足该要求的 provider 都必须把相应工作移到 Acquire 或 Ready。

non-module contribution 在`Probe`中由 category parser 完成解析，且不得产生运行时副作用。需要 Acquire、Ready 或运行时资源的 contribution 必须注册为 module category。

##### 失败与 rollback

Acquire 或 Ready 的每个成功步骤都必须记录在事务完成日志中。由于未 Commit 实例从未进入可执行状态，rollback 不对本事务新建的实例调用 quit 或 wait。当前失败对象负责撤销自己的半初始化资源，随后加载器按实际完成日志的反序直接销毁本事务创建的未发布资源，并释放本事务取得的临时强引用。

ImportBinding 必须写入事务完成日志。rollback 与正常卸载必须先销毁 importing module 拥有的全部 ImportBinding，再销毁 importing module，并在最后释放相应的 dependency edge。

释放临时引用后，引用计数仍大于零的已有实例保持加载。若某个此前已经 Commit 的实例因此降为零，则只对该实例按下述正常卸载规则执行 quit / wait。rollback 只撤销本事务增加的引用和创建的资源，不得撤销其他 handle、其他已 Commit Package 或其他 dependency edge 持有的引用。

每个成功步骤只能清理一次。已经按事务日志清理完成的未 Commit 实例在引用归零时只移除实例记录，不得再次释放同一资源。

最初导致事务失败的错误是 primary error。允许失败的 rollback 物理资源回收或资源销毁错误作为附加诊断报告，不得覆盖 primary error。销毁操作必须幂等。provider 未完成规范要求不可失败的逻辑 abort 属于违反本规范，此后的行为未定义；Runtime 不负责为这种不合法 provider 建立额外的 rollback 屏障、隔离或恢复流程。

##### release 与卸载

释放 load handle 时，根 Package 的外部强引用减一。实例总强引用计数降为零时，加载器必须先将实例从`Running`原子切换为`Stopping`，关闭其所有运行时外部入口，并将其从可见实例表中移除，然后依次执行以下两个停止原语。

进入`Stopping`后，由 task、session、ImportBinding、导出对象或其他既有 handle 发起的新操作必须在进入 Package 或 provider plugin 代码前返回错误，不得产生新的线程、任务、callback 或 in-flight 调用。状态切换前已经进入的调用可以继续退出，并由 wait 等待。管理方仍可调用停止与诊断所需的管理操作；这些操作不得重新打开运行时入口。

关闭 ImportBinding 不得立即销毁它。关闭前已经通过 binding 进入 importer 或 target 的双向调用可以继续退出，Package 的 wait 必须等待 owning Package 全部 binding 上的这些调用结束。只有 wait 成功，或下述执行域终止流程确认相关代码不可能继续执行后，加载器才能销毁 binding，再销毁 importing module，最后释放相应的 dependency edge。

1. **quit**：请求实例停止接收新工作，取消其注册的 callback，并通知其拥有的线程、任务及其他异步活动退出。quit 可以在活动尚未完全退出时返回，不构成已经停止的证明。
2. **wait**：等待上述活动、全部 ImportBinding 上已经进入的双向调用及其他所有仍归属于该 Package 或其 binding 的 in-flight 调用结束。wait 成功返回只证明实例已经静止：当前仍归属于该 Package 实例的所有执行活动均已停止，允许继续运行的隔离 continuation 已按上文完成重新归属；此后任何活动都不得再代表该 Package，通过线程、任务、session、import binding、callback、导出对象或调用访问该 Package 实例、其 binding、handle 或 dependency。隔离 continuation 只能访问由新 owner 独立持有的 target、provider 与其他资源。wait 不负责销毁仍归实例所有的模块或运行时资源。调用方仍持有的运行时 handle 必须已经失效，后续操作只能返回错误，不得再次进入相关 Package 代码。Runtime API 负责实现该保证，本规范不规定各类运行时对象内部使用的引用计数或保活机制。

只有 wait 成功，或下述执行域终止流程确认相关代码不可能继续执行后，加载器才能按成功加载步骤的反序销毁该实例的模块和运行时资源，再释放它持有的 dependency 强引用；由此可以递归卸载不再被任何 handle 或 Package 使用的依赖。资源销毁发生在已经停止之后，其失败作为卸载诊断报告，不阻止释放其他已经确认安全的资源与引用。

对于已经 Commit 的实例，wait 后的销毁是正常卸载中唯一仍可进入 provider 或 dependency 代码的 teardown 路径，只能执行同步清理。未 Commit 且从未激活的 Acquire / Ready 产物则按 rollback 完成日志直接执行同步 provider teardown，无需调用 quit 或 wait。两种销毁过程都必须完成并返回，不得重新打开任何运行时入口，不得启动线程、异步任务或 callback；对应销毁步骤返回后，不得再访问已经销毁的对象。本规范不定义 teardown 超时、失联检测或补救协议；无法完成并返回的 provider 不合法，后续行为未定义。执行域被强制终止时，只能清理宿主侧仍可安全销毁且不需要重新进入已终止执行域的资源。

每个 provider 的执行活动都属于一个由 Runtime 决定的执行域。执行域可以是当前宿主进程，也可以是由宿主管理的跨进程 provider。quit 返回错误只作为卸载诊断，Runtime 仍必须执行最终 wait barrier；只要 wait 成功证明实例已经静止，加载器就按正常路径 teardown。只有 wait 失败、超时、不可用或根本无法执行，因而最终 barrier 无法证明实例已经停止时，Runtime 才必须终止包含该实例全部代码与活动的最小故障隔离单元，并等待底层系统确认该单元已经终止；在确认前不得销毁实例、释放 dependency 引用或报告卸载成功。

跨进程 provider 可以在一个 worker 或一组节点中承载多个生命周期相互独立的实例，但必须提供实例级以及上述 ImportBinding 和调用级故障隔离，或提供等价的恢复能力。一个实例、binding 或调用的停止、卸载、超时或内部失败不得导致其他仍有有效引用的实例，或 owning Package 仍为`Running`的 active binding 永久失效。Runtime 不得为了清理其中一个而终止仍承载其他存活实例或 active binding 的唯一可用 worker。provider 可以使用独立 worker、多 worker、多节点、心跳、重新握手、请求重发、状态恢复、实例重建或其他机制满足该结果，本规范不规定具体机制。worker 或节点整体退出后，provider 必须恢复其中仍有有效引用的其他实例，以及 owning Package 仍为`Running`且故障前为 active 的 binding；`Stopping`、never-active 或已经关闭待 drain 的 binding 不得重新打开。恢复期间如何报告暂时不可用由 Runtime API 规定，不得把应恢复的实例或 binding 视为已经卸载。

不能满足上述隔离或恢复保证的跨进程 provider，不得在同一故障域内承载生命周期相互独立的多个实例。若 provider 与 Runtime 同处宿主进程，或实例的最小故障隔离单元就是宿主进程，则无法证明实例停止时必须 fail-fast 终止宿主进程，不得尝试在同一进程中恢复、继续加载 Package 或假装卸载成功。依赖图无环保证正常递归卸载能够终止。

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
    + `vars`：当前模块私有的字符串变量，见上文《字符串变量》
    + `name`：模块名称，可为多语言；字段缺失时等价于`{"_": id}`
    + `exports`：公开自己支持的功能集合
    + `configuration`：本模块自身的参数
    + `imports`：本模块引用的其他模块

`name`字段显式存在时必须按《多语言文本》原样保留，包括空字符串和 value 为空的 map；Runtime 不根据任何语言项是否为空执行回退。因而`"name": ""`等价于`"name": {"_": ""}`，不会替换为模块`id`。

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

> 2.3 中这个字段叫`class`，并被描述为「推理类型」。改名的原因是它命名的不是一个类，而是一份契约：`org.openvpi.*`是本规范定义的公共契约，第三方定义的新契约使用自己的命名空间（如`com.vendor.*`）。

`variant`区分同一契约下的不同实现变体。变体之间可以差在任何地方——模型的格式、所用的算法、乃至用不用模型（文本到音素的转换可以基于规则，也可以基于模型）。同一 (`interface`, `level`) 下的变体共享公共模块 envelope、`exports`与`options`语法及运行时契约，`configuration`可以不同，也可以公开不同的能力集合，不保证彼此可互换。

某个`variant`仍然是该契约的一种实现。共享`interface`表示它们使用相同的契约词汇和调用规则，不表示每种实现必须提供契约允许的全部能力。

`variant`由`interface`定域，故本规范定义的变体用裸词即可。本规范当前使用的裸 variant 包括 Inference 契约的`onnx`与 Singer 契约的`diffsinger`。第三方为他人的契约提供变体时应使用反向域名（如`com.vendor.tensorrt`）。

该字段必填，**没有「默认变体」一说**。每个模块都由某个具体的实现来读取和执行，把那个实现的名字写出来，加载器才能在找不到解释器时说清楚缺的是哪一个。

加载器先根据模块所属 category 取得该类别的 provider factory，再据`interface`、`variant`、`level`三者共同选择 provider。

#### 模块 provider 发现

每个模块类别拥有各自专用的 provider factory 和有序插件搜索路径。一个类别的 factory 只发现和创建该类别的 provider，不同类别的 provider 不在同一个搜索目录或候选集合中并列比较。category 因此是选择 factory 与搜索路径的外部上下文，不属于 provider 的匹配键。

模块 provider 必须在无需加载插件即可读取的插件元数据数组中声明它支持的`interface`、`variant`和`level`。同一插件的数组中不得重复声明相同的 (`interface`, `variant`, `level`) 三元组，重复时整个插件元数据无效。

一个目录项只有在其元数据能够读取、是合法 JSON、包含全部必选字段及正确类型，并通过包括三元组不得重复在内的全部结构验证后，才被视为 provider plugin。无法读取、解析或通过验证的目录项，以及扫描期间消失的目录项，均视为不是插件并直接跳过，不得进入候选集合，也不使 discovery 失败。普通文件按相同规则自然忽略。

`Probe`阶段先根据模块 category 取得对应 factory，并按用户给出的顺序扫描该类别的插件搜索目录。同一目录中的插件按 basename 的 Unicode 码点升序进行区分大小写的比较，插件发现顺序不得采用文件系统枚举顺序或 locale 排序。加载器按由目录顺序和目录内文件名顺序形成的全序扫描元数据，选择第一个三元组全部匹配的 provider；后续出现的相同三元组不参与选择。

上述 discovery 只用于首次创建新的 module instance。Acquire 成功后，实例必须记录实际 provider 与 provider plugin；Commit 后该绑定在实例的整个生命周期中不可改变。已 Commit Package 作为根 Package 或 dependency 被复用时，其全部 module instance 必须继续使用各自记录的 provider，不得因新增插件、修改插件搜索路径或目录内容变化而重新 discovery、改选或热切换。新增搜索目录和插件只影响之后首次创建的新实例。

本规范不要求插件元数据声明宿主 API 或 ABI compatibility，也不在 provider 选择阶段检查二进制兼容性。插件部署者必须保证插件二进制与当前 Runtime 兼容；选中的插件无法加载或存在二进制不兼容时，必须报告严重加载错误并使整次加载失败，不得尝试后续 provider。

上述宽松跳过规则只适用于 provider 被选中之前。一个具有合法元数据的 provider 一旦被选中，Runtime 在后续正常读取或加载过程中实际观测到其元数据或二进制无法读取、消失、加载失败或不再与选择结果一致时，属于部署错误，整次 Package 加载必须失败且不得回退到后续 provider。

provider plugin 被选中后，其元数据与二进制工件直到当前 Runtime 整体销毁前不得被修改、替换或删除。违反该稳定性要求属于部署错误，其行为未定义；Runtime 不负责监视或检测变化，也不保存工件快照、执行 stale 检查或切换到其他 provider。

provider plugin 成功加载后必须加入当前 Runtime 的常驻插件集合，并保持加载直到 Runtime 整体销毁或进程退出。Package rollback 和正常卸载均不得从该集合卸载 plugin，也不为 Package、module 或 ImportBinding 分别维护 plugin lease 或引用计数。多个 Package 和 module 可以共享同一已加载 plugin。

Runtime 整体销毁前，必须先关闭所有 provider execution domain 的运行时入口，停止并等待全部归属于这些 domain 的 continuation、scheduler、heartbeat 及其他活动，确认所有 domain 已静止。只有此后才能销毁 plugin 创建的 module、ImportBinding backend 及其他对象，最后卸载常驻 plugin。provider 必须使该停止过程完成；违反要求的 provider 不合法，后续行为未定义。本规范不规定 activity 注册、等待或执行域管理的具体实现。

`configuration`及其中由 variant 自行定义的格式版本不参与静态 provider 选择。选中的 provider 在`load`阶段发现不支持该`configuration`时，必须报告错误并使加载失败，不得尝试同类别搜索路径中的后续 provider。

#### interface 契约规范

每个 (`interface`, `level`) 必须对应一份由该 interface 所有者发布的、稳定且可定位的契约规范。官方 interface 由本规范的维护者发布，第三方 interface 由其命名空间所有者发布。同一个 (`interface`, `level`) 的契约发布后不得作不兼容修改；不兼容修改必须递增`level`或另起`interface`。

契约规范至少必须包含：

- `exports`的机器可读 JSON Schema
- `imports[].options`的机器可读 JSON Schema
- 运行时输入与输出的数据结构、数据类型、形状和单位
- 必选能力、可选能力及其语义
- 错误条件及其可观察语义

`configuration`不属于 interface 契约，其全部语法与格式版本均由`variant`规定。

契约文档与 JSON Schema 是面向 Package、provider 和消费者实现开发者的规范材料。“可定位”不表示 Runtime 必须从 interface 名称推导 URI、访问中央 registry、下载 Schema 或校验摘要。本规范不定义运行时 contract discovery 协议，Package 与插件元数据也不携带契约 URI 或 digest。

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
    + `ref`：被引用模块的 ModuleReference，见上文文法
+ 可选字段
    + `options`：提供给被引用模块的选项，其语法由**被引用模块**的`interface`与`level`规定

`imports`是由零至多个条目组成的有序数组。加载器必须保持声明顺序，不得排序或重排。每个条目都是独立的导入实例，相同`ref`可以重复出现，加载器不得自动合并或去重。

每个`imports`数组项必须恰好产生一个独立的 ImportBinding，由 importing module 拥有。ImportBinding 保存该条目的`ref`、`options`及其到目标模块的运行时连接；多个条目即使引用同一个目标模块，也不得共享或覆盖彼此的 binding 状态。目标模块实例可以共享，但不得将某一 import 的`options`作为目标模块的全局配置写入。

公共格式不提供本地 slot。导入项的用途通过被引用模块契约规定的`options`表达。导入模块需要多少项、某类导入是否必选，以及某种重复是否符合具体实现要求，由导入模块的`variant`验证。

**被引用的可以是任何模块类别中的模块，不限于 `inference`。** 类别写在`ref`里：`:inference/pitch`引用一个推理模块，`:com.vendor.language/cmn`引用一个第三方语言模块。非模块类别的贡献不能作为`imports[].ref`的目标。这也是为什么这里用一个引用串而不是拆成几个字段——拆开就没有类别的位置了。

`options`的语法**不依赖被引用模块的`variant`**。导入方按引用串选中具体模块，可以根据该模块的`exports`填写`options`；共享契约的另一个变体可以具有不同的`exports`，也就不保证能直接接受同一份导入声明。

ModuleReference 中显式出现的每个 Package ID 都必须在`desc.json`的`dependencies`中声明。引用当前 Package 时不得显式写出其 ID，而应使用以`:`开头的形式。

`ref`必须是一条完整的 ModuleReference，不设简写。2.3 中`imports`可以直接写裸字符串（如`"acoustic"`）表示「本 Package 的 inference 中名为 acoustic 的那个」，2.4 不再接受这种写法，其完整形式是`":inference/acoustic"`。

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
            "tension", "energy"
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

以下 Singer 声明是本章`desc.json`中`singer/zhibin`所指向的`./singers/singer1/singer.json`，并使用同一 Package 定义的变量、inference 模块与 dependency。

```json
{
    "interface": "org.openvpi.synthrt",
    "level": 1,
    "variant": "diffsinger",
    "name": "Zhibin",
    "vars": [
        { "name": "singerAssets", "value": "${assets}/singer1" }
    ],
    "avatar": "${singerAssets}/avatar.png",
    "background": "${singerAssets}/sprite.png",
    "demoAudio": "${singerAssets}/demo.wav",
    "imports": [
        { "ref": ":inference/acoustic" },
        {
            "ref": "bar:inference/pitch",
            "options": { "roles": ["pitch"] }
        },
        {
            "ref": ":inference/variance",
            "options": { "roles": ["tension", "energy"] }
        }
    ],
    "configuration": {
        "dictionary": "${singerAssets}/dsdict.json"
    }
}
```

Singer 类别在公共字段之外追加以下可选的多语言路径字段：

+ `avatar`：头像路径
+ `background`：可用于 SVS 编辑器显示的立绘背景路径
+ `demoAudio`：可用于 SVS 编辑器预览的声音路径

这三个字段对`singer`类别下的所有契约都可用，用不上的契约不写即可。`configuration`按变体规定的语法填写歌手实现参数，其中的字符串字段可以使用字符串变量；`imports`按公共字段的语法列出本歌手引用的其他模块，其中每个`options`由被引用模块的契约规定。

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

- 需要一种全新的模块（而不只是一种新的推理）时，扩展**贡献类别**：链接进宿主的库注册一个新类别，此后`desc.json`就可以在`contributes`下列出它，ModuleReference 文法也自动支持它。本规范不限制类别的集合。

### 推荐目录结构

```
+ somedspk
  + assets
    - readme.txt
    + singer1
      - avatar.png
      - sprite.png
      - demo.wav
      - dsdict.json
  + inferences
    + acoustic
      - inference.json
      - acoustic.onnx
    + variance
      - inference.json
      - variance.onnx
    - ...
  + singers
    + singer1
      - singer.json
  - desc.json
```

- 共享的资源文件放置在`assets`中，每个歌手可以使用与其模块对应的子目录，例如`assets/singer1`
- 推理模块放置在`inferences`的子目录中，每个子目录一个声明文件，歌手模块同理
- 根目录固定放置`desc.json`

## 3. 插件开发

每个 module category 都注册自己的 provider factory 与有序插件搜索路径序列。不同 category 的搜索路径彼此独立，某个 category 的 factory 只扫描该 category 的路径，不得从其他 category 的目录发现或选择插件。搜索路径由宿主配置，本规范不要求所有 category 共用某个固定的`plugins`根目录。

例如，某个宿主可以配置：

```text
inference:
  1. ./plugins/inference
  2. /opt/synthrt/plugins/inference
singer:
  1. ./plugins/singer
com.vendor.language:
  1. ./plugins/com.vendor.language
```

其中项目内的简单目录结构可以是：

```text
+ plugins
  + inference
    - onnx.plugin
    - tensorrt.plugin
  + singer
    - diffsinger.plugin
  + com.vendor.language
    - dictionary.plugin
```

上述文件名与扩展名只用于说明目录归属，不规定插件的二进制格式、元数据载体或命名规则。即使两个 category 的搜索路径在文件系统中发生重叠，各自 factory 仍只能识别本 category 的插件。

### 推理解释器

Inference provider 使用`inference`类别专用的插件搜索目录和 factory，是上文模块 provider 发现机制在该类别下的具体实现。

插件必须在插件元数据中列出它提供的推理解释器。每个解释器条目包含：

- `interface`：负责的契约，如`org.openvpi.svs.PitchInference`
- `variant`：负责的变体
- `level`：负责的那一个 API Level

插件元数据必须能在不加载插件的情况下读取。同一插件的解释器条目数组不得重复声明相同的 (`interface`, `variant`, `level`) 三元组。Inference factory 按上文规定的目录顺序与目录内文件名顺序扫描元数据，选择第一个与模块的`interface`、`variant`和`level`全部匹配的解释器条目。后续出现的相同三元组不参与选择。

选中条目后，加载器才加载对应插件并创建派生于`InferenceInterpreter`的解释器。

具体推理任务的创建、初始化、执行、取消、状态、错误与结果生命周期不属于本规范，由单独的运行时 API 规范定义。
