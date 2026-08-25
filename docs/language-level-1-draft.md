# DiffSinger 语言契约 Level 1（草案·契约版）

> 本文档定义 wolf 注册到 synthrt 的 `language` 贡献类别，以及语言组合所需的
> G2P / S2P / Onset 三种推理契约的 Level 1。**本文只钉契约面**：类别与三元组的
> 存在性、Exports/Options/Variables（即 `level` 承载的输入输出）、level 变更纪律、
> 以及宿主可见的组合与裁决语义。各变体的 `configuration` 键汇、资源格式、收录清单
> 等实现族细节由 wolf 的变体文档维护（当前暂存于 `docs/language-g2p-variants-wolf-draft.md`），
> 外部系统（编辑器、打包工具、声库作者）不依赖其实现细节。
> 上位规范：spec 2.4（`docs/ds-spec-2.4.md`）。Package、贡献类别、模块三元组、
> `imports`、ModuleReference、`dependencies` 等概念以该规范为准，冲突时以该规范为准。

## 前置说明

- **Exports**：契约词，模块向导入方公开的能力，由 `interface` + `level` 规定。
- **Options**：契约词，导入方在 `imports` 条目中提供给被引用模块的选项，由 `interface` + `level` 规定。
- **Variables**：契约运行时的输入与输出。
- **Configurations**：模块自身参数，由 `variant` 全权规定（spec 2.4）；
  - 本契约族各变体的键汇见 wolf 变体文档；
  - 路径以其所在声明文件目录为基，使用前已完成 `${vars}` 变量展开（spec 2.4《字符串变量》）。
- `$version` 只写在 `desc.json`，模块声明文件不携带 `$version` 与 `id`（模块 ID 由所属 Package 在 `contributes` 条目中赋予）。
- 机器可读形式：各 (interface, level) 契约的 exports/options JSON Schema 随契约
  发布物落位于 wolf 文档侧（spec 2.4《interface 契约规范》要求）；Level 1 草案期以本文表格为准。

## 1. `language` 贡献类别

### 1.1 注册

`language` 是 spec 2.4 意义下的**模块类别**，由 wolf 注册。

- wolf 是被宿主（编辑器、命令行工具等）直接链接的库，其注册在任何运行单元构建之前生效；
- 注册时一并登记该类别的 provider factory 与有序插件搜索路径（spec 2.4《贡献类别是开放的》）；
- 类别属宿主侧扩展，本类别注册名为 `language`；
- 宿主未链接 wolf 时该类别未注册，含 `language` 贡献的 Package 将被加载器整体拒绝。

### 1.2 贡献条目

```json
"contributes": {
    "language": [
        { "id": "cmn-pinyin", "path": "./languages/cmn-pinyin/language.json" }
    ]
}
```

- `id`：
  - 基本形为 `<iso-639-3>-<注音体系>` 全称（`cmn-pinyin`、`jpn-romaji`、`yue-jyutping`、`eng-arpabet`…）；
    同一语言的多种注音体系以多张 ID 并列（如 `cmn-pinyin` 与 `cmn-bopomofo`）；
  - 允许**追加一个**自定义字段作为作者标志或同包多方案的区分（如 `cmn-pinyin-v2`）；自定义字段无语义、不被剥除，匹配一律按 §1.5 的恒等规则以全 ID 判定；
  - ID 内以连字符分隔的部分称**字段**（iso／注音体系／可选自定义字段；与 spec 2.4 的 `segment` 不是同一层级的概念），各字段内部不得再含连字符；
  - ID 是结构标识符，遵守 spec 2.4 的 `segment` 文法，同 Package 同类别下不得重复。
- `path`：指向语言声明文件，约定 `languages/<id>/language.json`。

### 1.3 声明文件

三元组固定为 `("org.openvpi.lang.Language", 1, "wolf")`；本类别不追加字段。`name` 为多语言文本，缺省等价于 `{"_": <id>}`。

引用语言与引用其他模块同文法：`other-pkg:language/jpn-romaji`、`:language/cmn-pinyin`（当前包）。

### 1.4 Exports

|   name   |            type            |                                  description                                   |       example       |
|:--------:|:--------------------------:|:------------------------------------------------------------------------------:|:-------------------:|
| phonemes | path \| list&lt;string&gt; | **必选**。本语言默认封闭链（自带 G2P + S2P）末端可能产出的内容音素全集（注 1） | `"./phonemes.json"` |

- 注 1：保留音素（如SP、AP、EP）不进清单；写为路径时指向内容为 `list<string>` 的 JSON 文件。

说明：

- `phonemes` 是语言**链末端**与消费方的对齐基准：
  - 宿主据此核验实际组装链的 S2P 产物；
  - 模型侧对照各 stage 音素表；
- 语言包作者拥有默认链全部模块，故该全集可静态给出；
- 声库逐项覆写链路成员导致清单与实际产物不符的，属内容缺陷；是否拒绝由消费方决定，Level 1 不做加载期强制校验。

### 1.5 imports（组合规则）

语言的 `imports` 是本契约族唯一规范性的组合层，**Level 1 内**集合恒定：

| 角色  |             目标契约              |  数量  |                 用途                  |
|:-----:|:---------------------------------:|:------:|:-------------------------------------:|
|  G2P  |  `org.openvpi.lang.G2pInference`  | 恰好 1 |              歌词 → 发音              |
|  S2P  |  `org.openvpi.lang.S2pInference`  | 恰好 1 |            发音 → 音素序列            |
| Onset | `org.openvpi.lang.OnsetInference` |  0..1  | 音素 → onset 标记；缺省时由宿主合成全 `false` |

- 语言不感知 G2P 的内部组合（是否需要模型后端、几个后端，G2P通过`imports`设置），语言层的基数表因此恒定不变；
- 语言 `imports` 按目标契约角色匹配，序位不承载语义（本条仅指**语言模块**的 imports；歌手侧序位用法见 §5）；
- **语言与支持集合的匹配**：G2P提供 `exports.languages` 时，语言须命中其中一项（由 wolf 于语言模块加载期的 Ready 校验中执行，spec 2.4《加载事务》）。
  - G2P 导出的条目必须包括该语言的贡献 ID 全名（如语言 `cmn-pinyin-v2` 要求 G2P 导出 `cmn-pinyin-v2`）；
  - G2P 的实现细节不强制进入导出面：内部为 v2 实现的 G2P 仍可只导出 `cmn-pinyin`，内部差异由模块自身配置映射吸收（§2.1）；要对接带自定义字段的语言贡献时，则须导出同名全名 ID；
  - 语言侧显式 `options.languageId` 时按同一规则判定；目标未导出 `languages` 时
    不得提供本选项——双方导入导出不匹配，提供即语言模块加载失败；
  - 编辑器负责校验语言导出的 phonemes 为「声库各 stage 模型音素表交集」的超集；
- **目标 level 校验**：上表只约束目标契约，与level无关
  - wolf 在 Ready 校验中要求目标 interface 命中、且目标 level 属于本 provider
    插件元数据为该 interface 声明的 level 集合（spec 2.4《加载事务》）；
  - 被引用模块的运行时变量按其自身声明的 level 执行，本表基数与匹配规则不随目标 level 变化。
- 引用其他 Package 的模块时，谁 import 谁在 `desc.json` 的 `dependencies` 中声明。

### 1.6 Configuration 与 Variables

- Language 接口的 `wolf` 变体在 Level 1 不定义 `configuration` 必选字段，留待后续补充；
  语言固有的词典、规则与模型资源一律归于所导入模块的 `configuration`；
- 语言不承载运行时推理，无 Variables；
- 解释器（wolf）在加载阶段只校验上述恒定基数与契约三元组；模块之间的连线由各自解释器沿模块自己的 `imports` 完成；
- 语言加载失败则依赖它的歌手加载随之失败。

### 1.7 推荐目录结构

```
+ languages
  + cmn-pinyin
    - language.json     // 语言声明
    - phonemes.json     // exports.phonemes 所指文件（内联数组时可无）
    + g2p
      - inference.json  // G2P 模块声明（词典等资源就近放置）
    + s2p
      - inference.json  // S2P 模块声明（词典等资源就近放置）
    + onset
      - inference.json  // Onset 模块声明
```

## 2. `org.openvpi.lang.G2pInference` Level 1

歌词文本 → 发音（grapheme-to-phoneme）。

### 2.1 Exports

|   name    |            type            |                    description                    |                       example                       |
|:---------:|:--------------------------:|:-------------------------------------------------:|:---------------------------------------------------:|
|  symbols  | path \| list&lt;string&gt; | 本模块可能输出的原子符号全集（注 1）；形式同 §1.4 |                 `"./symbols.json"`                  |
| languages | path \| list&lt;string&gt; |       本模块支持的语言贡献 ID 集合（注 2）        | `["cmn-pinyin", "yue-jyutping"]`、`["eng-arpabet"]` |

- 注 1：输出串含保留定界符（空格）时按 §2.4 约定拆分后计入，无空格时整串即一个符号（音节等发音单元）；
- 注 2：一律为语言贡献 ID（形态规则同 §1.2，可带自定义字段），按 §1.5 的恒等规则匹配；常规实现导出基本形（如 `cmn-pinyin`），对接特定方案时导出自定义字段全名（如 `eng-arpabet-plus`）。

`symbols` 缺失时可加载，但宿主无法对 G2P 产出与其语言链末端音素做静态比对，要设置警告。

**契约暴露面只允许规范语言 ID**，实现内部的子资源标识（如 multig2p 的`eng/default`）不出模块边界：
暴露 ID 到内部子资源的映射由模块在自身配置内声明并处理（wolf 变体文档）。

### 2.2 Options

|    name    |  type  |                                 description                                 |     example     |
|:----------:|:------:|:---------------------------------------------------------------------------:|:---------------:|
| languageId | string | 指定目标模块支持的语言贡献 ID，须按 §1.5 的恒等规则命中 `exports.languages` | `"eng-arpabet"` |

仅当目标模块导出 `languages` 时可提供本选项；未导出时提供即双方导入导出不匹配，
按 §1.5 加载失败处理。

内部子资源标识（multig2p 家族内部的 `langRef`，如 `eng/default`）不进入契约词，模块收到 `languageId` 后自行映射（wolf 变体文档）。
`options.languageId` 只在导入期起绑定/选定作用，不构成运行时通道；运行时的语言值走 §2.4 的 `languageId` 变量。

### 2.3 Configurations

`configuration` 由 `variant` 全权规定（spec 2.4），本契约不设统一外层形态；
各变体键汇与资源格式见 wolf 变体文档。其中路径以本模块声明文件目录为基。

### 2.4 Variables

接口按批量词列表受理，本表按单元语义描述。

**Level 1 能力边界**：输入是有序的词列表。

- 句级语境参与的多音字消歧/同形异读属后续 Level 演进，Level 1 部分G2P已实现（如cpp-pinyin引擎）、但不做能力承诺；
- 解释器在多轮调用间维护会话（滚动上下文、修正窗重发）属实现自由，任务与状态生命周期由运行时 API 规范管辖（spec 2.4《推理解释器》）
  ——只要逐单元产出仍使用本节词汇表与定界约定，即不构成 Level 递增事由。

**输入合法性判定次序**（依序判定、先到先生效，均未命中者正常受理）：

1. `lyric` 为空串或仅含空白字符的 → 该词按 `mode=skip` 产出；
2. 其余含空白字符（词内或首尾空白）的 → `pronunciation` 写入原词透传，并携带非空 `error` 显式上报（此时 `mode`/`candidates` 依注 2 无定义）。

|   variable    |  I/O   |             type              | description                              |  activation condition   |
|:-------------:|:------:|:-----------------------------:|:-----------------------------------------|:-----------------------:|
|     lyric     | input  |            string             | 歌词单元（一个词/一个分片）              |            -            |
|  languageId   | input  |            string             | 语言贡献 ID（同 §2.2 规则）              | 模块导出 `languages` 时 |
| pronunciation | output |            string             | 主发音（注 1）                           |            -            |
|  candidates   | output |      list&lt;string&gt;       | 候选发音（多音），首个即主发音           |            -            |
|     mode      | output | enum：`convert`/`copy`/`skip` | 结果来源：转换 / 原词保留 / 空词跳过     |            -            |
|     error     | output |             enum              | 失败类型（值域枚举见各变体文档）（注 2） |            -            |

- 注 1：空格为发音层保留定界符——含空格即视该串为音素序列，无空格即待转换的发音。
  - 此定界约定使 Pronunciation 层无需区分两种变量类型，输出与输入同形对齐；
  - 发音串不携带数值标注（如置信度）；
- 注 2：非空即失败，词序位保留且 `mode`、`candidates` 之值无定义；成功时必须取空值——词级成败一律以本变量为准，兜底由宿主决定。

**运行时路径**：调用方以 iso 格式的语言句柄传入（如 `cmn`），宿主按歌手侧语言映射解析为
语言贡献 ID 后送达 G2P（§5）；未传入或解析失败的回退由变体文档规定（如 multig2p 的
`default_language`）。

**输出共现约束**（适用于 `error` 为空的产出；`error` 非空时以注 2 为准）：

- `mode=skip` 时 `pronunciation` 与 `candidates` 均为空；
- `mode=copy` 时 `pronunciation` 为原词、不得携带 `candidates`；
- `mode=convert` 时 `pronunciation` 即 `candidates` 首个元素。

### 2.5 变体治理

- 变体名属实现方命名空间：
  - 本契约族收录变体由 wolf 维护，`pipe-`/`algo-`/`nn-` 前缀与全部裸变体名为 wolf 官方保留；
  - 第三方按 spec 2.4 使用反向域名变体（如 `com.vendor.myengine`）。
- **变体粒度 = 分派粒度**：
  - 框架按 (`interface`, `variant`, `level`) 三元组匹配 provider（同一三元组被多个插件声明时按搜索全序取首个，spec 2.4《模块 provider 发现》）；
  - `configuration` 不参与 provider 选择；
  - wolf 保证其收录变体名单一承载，使首匹配规则不引入歧义；三元组命不中即「找不到提供者」加载失败。
- 收录变体清单、各变体的 `configuration` 键汇与资源格式：见 wolf 变体文档。spec 2.4 允许变体随时新增；新增不触碰本契约。

## 3. `org.openvpi.lang.S2pInference` Level 1

发音字符串 → 音素序列（symbol-to-phoneme）。

- 本契约 Exports 与 Options 均为空；
- 输入的多形态已由 §2.4 的空格定界约定消除；预音素化用途（宿主已有音素序列、仅需切分/映射的输入）应绕开 G2P、直调本接口；
- 收录变体清单与各变体 `configuration` 键汇见 wolf 变体文档。

### 3.1 Variables

|   variable    |  I/O   |        type        | description                                | activation condition |
|:-------------:|:------:|:------------------:|:-------------------------------------------|:--------------------:|
| pronunciation | input  |       string       | 发音字符串（G2P 的主发音）                 |          -           |
|   phonemes    | output | list&lt;string&gt; | 音素序列（未命中时的产出语义见各变体文档） |          -           |

本接口逐调用 IO 无跨词、跨调用语境；会话内上下文（如联诵缓存）属变体实现自由，不进入 Level 1 词汇。

## 4. `org.openvpi.lang.OnsetInference` Level 1

音素序列 → onset 位置标记。

- 本契约 Exports 与 Options 均为空；
- 收录变体清单与 `configuration` 键汇见 wolf 变体文档。

### 4.1 Variables

| variable |  I/O   |        type         | description             | activation condition |
|:--------:|:------:|:-------------------:|:------------------------|:--------------------:|
| phonemes | input  | list&lt;string&gt;  | 音素序列（S2P 的输出）  |          -           |
|  onsets  | output | list&lt;boolean&gt; | 与输入等长的 onset 标记 |          -           |

本接口逐调用 IO 无跨词、跨调用语境；会话内上下文（如联诵缓存）属变体实现自由，不进入 Level 1 词汇。

## 5. Singer 侧配套

歌手用 `imports` 声明支持的每种语言，无 `options` 词汇：

```json
"imports": [
    { "ref": ":inference/duration" },
    { "ref": ":language/cmn-pinyin" },
    { "ref": "wolf/lang-jpn:language/jpn-romaji" }
]
```

`singer` 类别在 spec 2.4 的 `avatar`/`background`/`demoAudio` 之外追加字段
（下表为**对 ds-spec 的扩展提案**，随下一版 spec 并入；类别追加字段的权属归 spec 维护者）：

|       name       |        type        |                                                      description                                                       | example  |
|:----------------:|:------------------:|:----------------------------------------------------------------------------------------------------------------------:|:--------:|
| defaultLanguage  |       string       | 默认语言的**语言句柄**（iso 首字段）建议值，供编辑器自行选择回退时参考、**不强制**；建议命中本歌手 `imports` 引用过的某个语言的句柄；未给出时编辑器可取歌手 `imports` 声明序中第一个语言的句柄 | `"cmn"`  |
| reservedPhonemes | list&lt;string&gt; |                               声库级跨语言保留音素（发声/短语事件类，如 `EP`）；缺省为空                               | `["EP"]` |

**语言结构体映射键（供 spec 作者参考）**：spec 侧歌手语言结构体（现行实现即
`configuration.languages[]` 条目：iso 标准格式 `id`、本地化 `name` 等）如需与语言模块
显式关联，可补一个映射键（如 `module`），值为指向语言贡献的 ModuleReference：

```json
"languages": [
    { "id": "cmn", "name": { "_": "Mandarin", "zh-CN": "普通话" }, "module": ":language/cmn-pinyin" }
]
```

该键把 iso 句柄条目与 `iso-注音体系` 形式的语言贡献 ID 显式绑定，编辑器与校验器无须再按
「首字段推导」回推；被指向的语言模块仍须列入歌手 `imports`（绑定仍由 `imports` 承担），
映射键只做显式关联。同一结构体数组内 `id`（句柄）唯一，与本节唯一映射约束同义。
键名与采纳与否归 spec 维护者；无论该结构体居 `configuration`（现行）还是升格为类别追加
字段（目标设计），提案同样适用。

其他规则：

- 语言贡献 ID 的第一个字段即 iso-639-3 代码（§1.2），称为该语言的**语言句柄**；
  `defaultLanguage`、上游 stage 的 `configuration.languages` 键名与宿主展示一律使用语言句柄；
- **唯一映射约束**：同一歌手 `imports` 引用的语言模块，其语言句柄必须互不相同——
  即同一声库中每个语言同时只支持一种注音体系；违背即歌手声明加载失败
  （由歌手变体解释器于加载期校验其 `imports` 集合，spec 2.4《加载事务》）；
- 作为类别追加字段，它们在解释器选定之前即可被宿主读取（编辑器列歌手时可见），校验由 `singer` 类别承担；
- 歌手的语言信息全部经语言贡献表达，`configuration` 不再携带任何语言列表或词典路径
 （目标设计；现行实现中歌手的 `defaultLanguage`/`languages` 仍居`configuration`，随迁移收敛）。

**保留音素**分两层：

- `reservedPhonemes` 声明**声库级**跨语言保留字；
- 编辑器级保留（`AP`/`SP`）由宿主注入会话、校验永远放行；
- 保留音素只豁免宿主对语言产出的逐词校验，能否编码进模型仍由各 stage 词表交集裁决——两层互不替代。

## 6. 公共语言包（消费规则）

- 公共语言包由 **wolf 项目发布**（随 wolf release 出新版本），编辑器负责内置随发行；安装位置不限；
- 公共语言包由 wolf 统一发布维护、规范性最强，依赖求解时**公共包的搜索路径的优先级必须高于声库内置G2P路径**；
- 公共包是普通 Package，安装、解包安全、依赖求解全按 spec 2.4；
- **已发布包目与支持语言列表由 wolf 维护并随版本更新，以 wolf 文档为准，本文不列举**；
- 包 ID 一律为 `wolf/lang-<iso-639-3>`（如`wolf/lang-cmn`），声库书写 `dependencies` 时按此命名；
- 一个公共包可包含同一语言多种注音体系的多张语言贡献（如 `cmn-pinyin` 与 `cmn-bopomofo` 同处 `wolf/lang-cmn`），歌手按 §5 的唯一映射约束择一导入；
- 每个公共语言包是完整语言闭包：
  - `contributes` 同时含 `language` 与 `inference`（本语言的 G2P、默认 S2P、默认 Onset），其语言声明按需导入本包内模块与其他公共包模块；
  - G2P如需模型后端，由G2P模块自己的 `imports` 引用共享后端包，语言声明不携带后端。

声库的两种用法：

1. **整体引用**（推荐、绝大多数声库）：`dependencies` 声依赖公共语言包，歌手直接 `imports` 公共包中的语言贡献（如 `wolf/lang-cmn` 中的 `language/cmn-pinyin`）。
2. **逐项覆写**（需要自定义的声库）：在本包写该语言的贡献，把公共模块（如公共G2P）与本包私有模块（如自定义 S2P 词典）混装进自己的 `imports`；
   `dependencies` 按实际引用声明。声库内私有 G2P 即本包普通的 `inference`贡献，不再有任何特殊机制。想替换公共G2P的资源或后端接线的声库，覆写
   该G2P模块的整份资源。
