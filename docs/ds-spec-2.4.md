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
| `contributes` 的属性名 | `inferences` / `singers`（复数，固定两种） | 贡献类别名（`inference` / `singer` / …），**集合开放** |
| `contributes` 的条目 | 对象，含 `id` `class` `configuration` | 对象，仅 `id` 与该类别自定的字段 |
| 模块 ID | 写在模块自己的声明文件里 | **写在 `desc.json` 里，由 Package 赋予** |
| `class` | 推理类型 + 解释器选择，一词两义 | 改名 `interface`，只表示「这份声明遵循哪套契约」 |
| 实现变体 | 无，只能另起一个 `class` | 新增 `variant`，与 `interface` 分为两根正交的轴 |
| 术语 | Library（亦称 Package） | 统一用 **Package**，不再用 Library / lib |
| `$version` | 每份模块声明文件各写一份，`desc.json` 反而没有 | **只写在 `desc.json`**，全 Package 共用一个 |
| 模块声明文件的公共字段 | 各类别各自定义、各自解析 | 提取为《公共字段》，由框架统一解析 |
| `schema` | 名为 schema，装的却是功能清单 | 改名 `exports`，与 `imports` 成对 |
| `imports` 条目 | `id` + `inferenceId` + `version` 三个字段，或一个裸字符串简写 | 一个 `ref` 引用串，**类别是其中一格**，不设简写 |
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
    + `version`：版本号，格式为`x.y[.z.w]`
+ 可选字段
    + `compatVersion`：兼容到的最低版本，如果为`0.0.0.0`表示向下兼容所有，如果与`version`相同则表示不向下兼容，缺省为不向下兼容
    + `vendor`：提供者，可提供多语言，多语言形式如下（`_`指定首选名称）
        ```json
        {
            "_": "Tadokoro Koji",
            "zh_CN": "李田所",
            "ja_JP": "田所浩二"
        }
        ```
    + `copyright`：版权信息，可为多语言
    + `description`：介绍文字，可为多语言
    + `readme`：放置介绍、许可证等信息的文本
    + `url`：网站
    + `contributes`：功能贡献列表，见下节
    + `dependencies`：依赖的库，缺省为空
        + `id`：依赖库 ID
        + `version`：依赖库版本
        + `required`：是否为强制依赖，默认为`true`

加载器应在解析`contributes`之前先检查`$version`，遇到不认识的版本应当整包拒绝，而不是继续解析后面的字段。这也正是它必须写在`desc.json`而非各模块声明文件里的原因——`desc.json`是最先被读到的文件。写在模块声明文件里的版本号，要等到整个 Package 已被接受、依赖已被解析之后才轮得到检查，那时拒绝已经太晚。

`dependencies`中的每个 Package 各自携带自己的`$version`，在其被加载时独立检查，不要求与本 Package 一致。

#### 功能贡献

`contributes`的每个属性名是一个**贡献类别**，其值是该类别下的模块列表。

```json
"contributes": {
    "inference": [
        { "id": "pitch", "path": "./inferences/pitch/inference.json" }
    ]
}
```

每个条目：

+ 必选字段
    + `id`：模块 ID。**由 Package 赋予，不是模块自身的属性**——它是别处引用这个模块时使用的名字，因此模块自己的声明文件中不出现它。同一类别下不得重复。
+ 其余字段由该类别自行规定。`inference`与`singer`都只需要一个`path`指向模块的声明文件。别的类别可以另有形状，甚至可以完全内联而不指向文件。

> **为什么 `id` 在这里而不在模块里**
>
> 引用一个模块（见下文引用文法）解析的是「某个 Package 对它的称呼」。放在这里有三个后果：
>
> - 同一份模块目录被两个 Package 收录时，两边可以各自命名
> - 一个 Package 对外提供了什么，从它的`desc.json`一个文件就能列全
> - 解析一条引用不需要打开被引用模块的声明文件

#### 贡献类别是开放的

**本规范不规定贡献类别的完整集合。** `inference`与`singer`是本规范定义的两种，加载器实现可以再注册别的（例如语言资源），而它们在`desc.json`中的写法与上面完全一致。

一个加载器遇到自己不认识的类别时应当报错而不是忽略——它无法判断这个类别是否是这个包正常工作所必需的。

#### 依赖项

当前 AI 推理现状是，一个模型动辄超过 100MB 甚至 1GB，因此内存与显存是宝贵的，本规范使用了一种常见的方式缓解这个问题。

为了模块复用与增量更新，`dspk`引入了依赖机制。

- 模块复用：开发者要分发若干个`dspk`（假设为 A、B），但是它们之间存在一些可以复用的内容，为了节省存储时的硬盘资源以及推理时的内存资源，那么可以将这些内容独立到一个`dspk`（C）中，在 A 与 B 的描述文件中声明它们依赖 C 即可。

- 增量更新：开发者要分发一个`dspk`（A），A 中存在稳定与不稳定的部分，不稳定的部分每次更新都需要更改，为了节省网络资源，那么可以将不稳定的内容独立到一个`dspk`（B）中，每次更新时只需更新 B 即可。

### 引用文法

引用一个 Package，或引用某个 Package 中的一个模块，使用统一的文法：

```
reference     = package-part [ ":" contrib-part ]
              / ":" contrib-part
package-part  = package-id [ "=" version ]
contrib-part  = [ category "/" ] contrib-id

package-id    = segment *( "/" segment )
category      = segment
contrib-id    = segment
segment       = 1*( ALPHA / DIGIT / "_" / "-" )
```

```
vendor/sample=1.0.0.0:inference/pitch    // 完整形式
vendor/sample=1.0.0.0:singer/main
vendor/sample=1.0.0.0:pitch              // 类别留待解析
vendor/sample:singer/main                // 版本留待解析
:singer/main                             // 当前 Package
vendor/sample=1.0.0.0                    // 指向 Package 本身
```

+ 没有 package 部分时，前导的`:`**不可省略**——`singer/main`指的是名为`singer/main`的 Package，不是`singer`类别下名为`main`的模块。
+ 类别可以省略，此时解析时搜索所有类别，命中多于一个则报歧义错误。
+ **类别是文法中的一格数据，而不是标点。** 新增一种贡献类别不需要改动这条文法。

### 安装与加载

#### 安装

对于`dspk`文件，安装就是在某个目录中解压它。本规范对这个目录没有任何要求。

#### 加载

由于`dspk`引入了依赖机制，因此一个`dspk`的加载流程中包含依赖查找。

- 对于一个`dspk`，当且仅当它的所有依赖项都能被加载，它才能被加载。
- 一个`dspk`的依赖项给定了所需依赖项的`id`与`version`，只有满足以下条件才能被视为合法依赖。
    - `id`与给定`id`一致
    - `version`大于等于给定`version`
    - `compatVersion`小于等于给定`version`
- 一个`dspk`将在其依赖项全部加载后进入初始化，初始化每一步都成功后即为成功加载。

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

加载器可由用户在启动时指定一个路径列表（如`~/.diffsinger/packages;/opt/diffsinger/packages;./packages`），当加载器加载某个`dspk`并伴随着解析其依赖时，加载器将依次遍历这个列表，尝试每个路径，如果在这个路径中找到了符合条件的依赖则加载之，并按同样的方法解析下一个依赖，直到结束。

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

后两层的分界在**什么时候读得到它**：类别追加的字段在解释器被选出来之前就要用上（SVS 编辑器要把已安装的歌手连同头像一起列出来，那时还没决定由谁来跑它），所以由类别自己解析。`exports`与`configuration`的内容则要等契约定了才知道怎么读，交给解释器。

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

`variant`区分同一契约下的不同实现变体。变体之间可以差在任何地方——模型的格式、所用的算法、乃至用不用模型（文本到音素的转换可以基于规则，也可以基于模型）。本规范不限定这根轴上可以出现什么，只要求：同一契约的各个变体输入与输出完全一致，对导入方而言可互换，差别只在于由谁解析、由谁执行。

之所以称为「变体」而非「实现」：某一契约的变体仍然是该契约的实例，二者对导入方而言可互换。若两者不可互换，它们本就不该共用一个`interface`。

`variant`由`interface`定域，故本规范定义的变体用裸词即可（当前只有`onnx`一种），第三方为他人的契约提供变体时应使用反向域名（如`com.vendor.tensorrt`）。

该字段必填，**没有「默认变体」一说**。每个模块都由某个具体的实现来读取和执行，把那个实现的名字写出来，加载器才能在找不到解释器时说清楚缺的是哪一个。

加载器据`interface`、`variant`、`level`三者共同选择解释器。

#### 三个语法块的归属

`exports`、`options`、`configuration`各由谁规定，取决于**读它的人认不认识变体**：

| | 谁读 | 读者认识变体吗 | 语法由谁规定 |
|---|---|---|---|
| `exports` | 导入方 | **不认识**——它是按引用串选中你的 | `interface` + `level` |
| `options` | 被引用模块的解释器 | 由导入方**书写**，故同样不能依赖变体 | `interface` + `level` |
| `configuration` | 解释器自己 | **认识**，它就是该变体 | `interface` + `level` + `variant` |

由此，**同一 (`interface`, `level`) 下的所有变体共享同一套`exports`语法**。它们用同一套词汇描述自己，但说出的内容可以不同——一个声明支持某项功能，另一个声明不支持。词汇表相同而内容不同，这正是可替换性的定义。

`configuration`中，`interface`与`level`规定必须存在的公共字段（例如声学模块与声码器模块之间需要交叉校验的采样率、帧移等），其余字段由变体自行规定。

本规范**不为变体设立单独的版本号**。`configuration`中属于变体的那一部分，其语法完全由该变体掌握，若需版本化，可自行在其中约定，无须本规范介入。

#### `imports`

每个条目：

+ 必选字段
    + `ref`：被引用模块的引用串，见上文引用文法
+ 可选字段
    + `options`：提供给被引用模块的选项，其语法由**被引用模块**的`interface`与`level`规定

**被引用的可以是任何类别的模块，不限于 `inference`。** 类别写在`ref`里：`:inference/pitch`引用一个推理模块，`:language/cmn`引用一个语言模块。这也是为什么这里用一个引用串而不是拆成几个字段——拆开就没有类别的位置了。

`options`的语法**不依赖被引用模块的`variant`**。`options`由歌手书写，而歌手是按引用串选中模块的。若其语法随变体而变，把一个声学模块换成另一种变体的声学模块，就会使歌手的声明文件失效。

引用中出现的每个 Package ID 都必须在`desc.json`的`dependencies`中声明。

`ref`必须是一条完整的引用串，不设简写。2.3 中`imports`可以直接写裸字符串（如`"acoustic-1"`）表示「本 Package 的 inference 中名为 acoustic-1 的那个」，这种写法与上文的引用文法冲突——裸字符串在文法中只能归约为 package-id，即一个名叫`acoustic-1`的 Package。旧写法对应的完整形式是`":inference/acoustic-1"`。

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

Inference 类别不追加任何字段。其中`exports`公开该模块支持的功能集合，`configuration`填写模型相关参数，二者的语法由契约规定。

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

Singer 类别在公共字段之外追加以下可选字段：

+ `avatar`：头像
+ `background`：可用于 SVS 编辑器显示的立绘背景
+ `demoAudio`：可用于 SVS 编辑器预览的声音

这三个字段对`singer`类别下的所有契约都可用，用不上的契约不写即可。`configuration`填写歌手相关参数，`imports`列出本歌手引用的其他模块，二者的语法由契约规定。

### 关于 API Level

由于 DiffSinger 引擎架构的复杂性，我们为模块引入了 API Level 的概念，在声明文件中用`level`表示。

API Level 代表某一份`interface`的版本号，是一个正整数，每当该契约的输入或输出格式发生更新时，它将向上递增。引擎官方为每个 API Level 制定一套描述模型功能的语法。

- Inference 模块用`level`声明自己所属的 API Level，在`exports`中按该 Level 的语法公开支持的功能集合，在`configuration`中按该语法填写模型参数。
- Singer 模块用`level`声明自己所属的 API Level，在`configuration`中填写歌手参数。
- Singer 在每个`imports`条目的`options`中填写它选用的那部分功能，其语法由**被引用模块的**`interface`与`level`决定，而不是歌手自己的。

API Level 版本化的是**`interface`**，既不是模块，也不是变体。模块用`level`声明「我说的是这份契约的第几版」。若 Level 按变体计算，两个变体的「Level 1」便不再是同一件事，可替换性当场失效。

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

- 需要为既有契约增添一种实现变体时（如在 ONNX 之外再支持别的模型格式），不必新起`interface`，只需取一个新的`variant`名并提供相应的解释器。对所有导入方而言新旧变体可互换，它们的声明文件无须改动。

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

创建派生于`InferenceInterpreter`的解释器类。

- `interface`：返回它负责的契约，如`org.openvpi.svs.PitchInference`
- `variant`：返回它负责的变体
- `apiLevel`：返回它负责的那一个 API Level
- `create`：创建对应的推理任务类

上面三项在解释器被实例化**之前**就必须可读——加载器据此从候选中挑选，挑中之后才调用`create`。

### 推理任务

创建派生于`Inference`的推理任务类。

- `initialize`：初始化推理任务，应当加载需要用到的模型
- `start`：开始推理任务，应当对输入的参数进行预处理，并构建推理图
- `startAsync`：`start`的异步版本
- `stop`：立即停止推理任务（同步）
- `state`：推理任务状态
- `result`：推理结果
