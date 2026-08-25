# wolf 语言域收录变体参考（G2P / S2P / Onset）

> **状态**：本文承载《语言契约 Level 1》（synthrt 侧契约草案）迁出的全部实现族细节，
> 属 **wolf 实现文档**而非对外契约；待迁入 wolf 仓库 docs/ 后删除本仓暂存。
> 上位契约：语言类别、`org.openvpi.lang.*` 契约面、`level` 纪律见语言契约 Level 1；
> 上位规范：spec 2.4（`docs/ds-spec-2.4.md`），冲突时以 spec 为准。
> 本文变更（step 词汇、配置键汇、资源布局）不触碰对外契约。

## 1. 总则

### 1.1 变体治理

- 变体名属实现方命名空间：
  - 本契约族收录变体由 wolf 维护，`pipe-`/`algo-`/`nn-` 类前缀与全部裸变体名为
    wolf 官方保留；
  - 第三方引擎/家族按 spec 2.4（:561）使用反向域名变体（如 `com.vendor.myengine`）。
- variant 以「类-族」编码成单个 `segment`（结构标识符文法允许连字符），
  **变体名即分派键**：
  - 框架按 (`interface`, `variant`, `level`) 三元组唯一选中 provider，
    `configuration` 内容不参与 provider 选择（spec 2.4《模块 provider 发现》，
    :565/:571/:589）；
  - 同一变体名只许一个 provider 插件承载，三元组命不中即「找不到提供者」加载
    失败，不存在运行期再分派；
  - 模块 ID 区分同一引擎下的具体产品打包（如 `multig2p`）。
- 推理会话能力经独立驱动注入：
  - 变体名中的字样只声明该家族绑定哪类驱动；
  - 会话创建本身不属变体职责。
- spec 2.4 允许变体随时新增；新增不触碰契约文档。

### 1.2 configuration 公约

`configuration` 由 `variant` 全权规定（spec 2.4:603/:619），契约不设统一 envelope；
本族变体直携自身键汇。约定两条：

1. **路径解析**：
   - `configuration` 中的路径以模块声明文件目录为基（已完成 `${vars}` 展开）；
   - 经路径键定位的家族配置文件，其内部路径再以该文件自身目录为基。
2. **格式版本自验**：
   - 配置（内联键汇或家族配置文件）顶层可携 `formatVersion`（单调递增正整数）；
   - 解释器声明自身支持上限；读入超过上限即加载失败、不得回退，诊断携带
     「声明版本 > 支持上限」与升级指引；
   - `level`（模块三元组格）管契约能力，`formatVersion` 管变体内部格式演进，
     两层互不替代；
   - 各变体该键必/选见下文。

## 2. G2P 变体清单

同一 (`interface`, `level`) 下，变体之间的差别只剩**实现大类与外部依赖剖面**：

- **规则算法**（`algo-` 类，无外部推理依赖）：资源即规则与词典；
- **神经网络**（`nn-` 类，依赖外部推理后端）：模型文件驱动会话，可独立作主转换器，
  也可被编排链引作后端——同一份模块两种用法，地位等同；
- **编排**（`pipe-` 类，依赖其他 G2P 模块）：组合式 G2P，依赖经 `imports` 显式暴露；
  - 当前唯一成员 `pipe-chain`（线性管道）；DAG 编排、条件路由、流式处理等
    未来风格同属此类并共用此前缀。

| variant | 大类 | 外部依赖 | 实现要点 | 详见 |
| :--: | :--: | :--: | :-- | :--: |
| pipe-chain | 编排 | imports 的 G2P 模块 | 打标校验 → 词典 → 模型 → 规整 → 兜底；唯一经 `imports` 暴露依赖的变体 | §3 |
| algo-cpp-pinyin | 规则算法 | 无 | cpp-pinyin 引擎（普通话/粤语：查表 + 规则转换） | §4 |
| nn-onnx | 神经网络 | ONNX 驱动 | 首个收录家族：multig2p；可独立作主转换器，也可被 pipe-chain 引作后端 | §5 |

演进判定（变体粒度 = 分派粒度）：

- **同族新成员**：新算法引擎、新模型家族（如未来的西语规则引擎、`lstm` 家族）→
  登记同级新变体名（`algo-spanish`、`nn-…` 式），不挤占现有变体；
- **新实现大类**（上述三类之外）→ 发明新类前缀，属契约族级变更，契约文档届时修订；
- 轻量、附属于组合链的规则逻辑走 pipe-chain 的 `rules`/`lua` step（资源格式演进），
  不构成新变体。

## 3. pipe-chain（编排类）

- 模块声明的 `configuration` 只有一个键：`chain`（path，**必选**），指向链配置文件
  `chain.json`；
- `chain.json` 必填顶层 `formatVersion`（§1.2 公约 2）；
- 模块声明的 `imports` 引用本链所需的模型后端，按 spec 2.4 显式暴露：

```json
// 公共包 wolf/lang-eng 内（示例）
// inferences/g2p/inference.json
{
    "interface": "org.openvpi.lang.G2pInference",
    "level": 1,
    "variant": "pipe-chain",
    "exports": {
        "symbols": "./g2p/symbols.json",
        "languages": ["eng-arpabet"]
    },
    "configuration": { "chain": "./g2p/chain.json" },
    "imports": [
        {
            "ref": "wolf/g2p-multi:inference/multig2p",
            "options": { "languageId": "eng-arpabet" }
        }
    ]
}
```

资源目录内部约定：

```
+ g2p
  - chain.json     // 步骤排布：{ "formatVersion": 1, "steps": [ { "step": <类型>, "params": <对象> } ] }
  - symbols.json   // exports.symbols 所指文件
  - *.txt          // 各步引用的词典等资源
```

Level 1 的 step 类型集合：

| step | params | description |
| :--: | :--: | :--: |
| tagAndValidate | `tagger: [{type, value, action}]`（注 1） | 分片打标 |
| dict | `{enabled, file}` | 词典查表 |
| model | `{enabled, batchSize}`（注 2） | 声明消费一个后端 |
| format | `{cleaner: {operations: [...]}}` 等（注 3） | 文本规整 |
| fallback | `{useOriginal, defaultPronunciation}`（注 4） | 兜底策略 |

- 注 1：`type ∈ regex/array/dict`，`action ∈ convert/copy`；
- 注 2：**步内不写任何模块引用**，后端取本模块 `imports` 中同序位的条目；
- 注 3：操作如 `lowercase`；
- 注 4：命中兜底的词 `error` 置 `PhonemeGenerationFailed`。

step 词汇与 `formatVersion` 是链家族的内部事务，与 `interface` 的 level 分层管理：

- 新增**纯内部** step（如 `rules`/`lua`，把西语式规则算法做成几十条规则而非整词
  词典；或不涉及外部推理能力变化的 `dict` 增强）→ 只升资源 `formatVersion`，
  模块声明的 `level` 不变；旧解释器遇到更高 `formatVersion` 一律拒绝加载；
- 新增依赖外部 G2P **更高 Level 能力**的 step（如未来调用 Level 2 推理契约的
  model 步变体）→ 使用该 step 的 pipe-chain 模块自身必须声明更高 `level`；
  全部 G2P 变体共享同一 IO（契约 §2.4），跨 level 只有整体切换，不存在混用。

**执行细则**：

- 解释器支持 `[1, 自身上限]` 区间全部 `formatVersion`；高于上限按 §1.2 公约 2
  诊断处理，缺键、非正整数、文件不可解析同按加载失败处理；
- 版本只增不减——旧配置遇新插件无缝，反向错误路径明确；
- 打包/编辑器等静态工具不加载插件即可读 `formatVersion` 预检。

**绑定规则**：

- `imports` 中第 *i* 个 G2P 条目注入第 *i* 个启用的 model 步（声明顺序对应），
  连同其 `options.languageId` 一并生效；
- 条目数与启用 model 步数不等，即本模块加载失败。

**覆写语义**：声库按语言契约 §6 逐项覆写链配置文件（整份资源）时，
`formatVersion` 语义不变：

- 仍由承载本三元组的 provider 按其支持上限校验；
- 覆写不得抬升或重置版本序列。

**model 步行为**：

- 仅处理打标为 `convert`、未命中词典、尚无发音的词（清洗后词优先）；
- 按 `batchSize` 分批调用后端；
- 后端调用失败标 `ModelInferenceFailed`，不可用标 `DriverUnavailable`；
- 后端（被引 G2P 模块）逐词返回的 `error` 原串透传为链输出的 `error`、不叠加
  本链枚举值；
- 携带 `error` 的词仍按本链 `fallback` 步的既有规则处理。

## 4. algo-cpp-pinyin（规则算法类）

- 本变体由唯一 provider 插件承载；新算法引擎登记同级新变体名（§2）；
- `configuration` 直接携带下列键汇（与现行实现直读 configuration 对齐）；
- 本变体无配置文件，`formatVersion` 不适用；
- 键汇演进以**拒绝未知键**守住——加载时遇未知键即失败，防拼写错误静默生效：

| name | type | description | example |
| :--: | :--: | :--: | :--: |
| scheme | enum | 引擎内转换方案（注 1） | `"mandarin"` |
| dictPath | path（相对声明文件目录） | 引擎词典目录 | `"./dict"` |
| verify | list&lt;object&gt; | 前置校验（注 2） | — |

- 注 1：cpp-pinyin 收录 `"mandarin"` \| `"cantonese"`，取值随引擎支持面扩充；
- 注 2：对象形 `{type: "regex"\|"array"\|"dict", value: [...], mode: "copy"\|"convert"}`，
  三键均必填；`dict` 型 value 为相对声明文件目录的路径。

- `exports.languages` 为本引擎支持的语言贡献 ID 集合（mandarin → `["cmn-pinyin"]`、
  cantonese → `["yue-jyutping"]`）；
- 同一语言家族贡献多个语种时，按「每语种一个模块实例、各自独立的资源配置」贡献：
  现行 cpp-pinyin 引擎词典路径为进程全局态，单实例多语种暂不受支持
  （实现侧已知约束，不影响契约）。

## 5. nn-onnx（神经网络类）：家族 multig2p

本变体名即分派键：同一 ONNX 后端的新模型家族登记同级新变体名（§2），本节只描述
multig2p 家族的配套约定。`configuration` 直接携带下列键（不设独立运行时配置文件）：

| name | type | description | example |
| :--: | :--: | :--: | :--: |
| bundle | path | **必选**。模型 bundle 根目录；`bundle.json` 与 `vocabulary.json` 与该目录邻接 | `"./model"` |
| inference | object | 推理参数键集，全部可选（注 1） | `{ "default_beam_size": 4 }` |
| languageMap | map&lt;string, string&gt; | 可选。语言贡献 ID → 内部 langRef 映射（注 2），缺省恒等（注 3） | — |

- 注 1：键集 `{default_max_len, default_beam_size, default_top_k, length_penalty, default_language}`。
  `default_language` 为缺省语言 ID（**暴露层 ID**，语义同 `languageId`，经
  `languageMap` 映射，不是内部 langRef）；数值键缺省属实现自由（类比 spec 2.4
  《何时递增 Level》采样步数之例）；`default_language` 缺省时按下文 langRef
  生效顺序判定。
- 注 2：如 `"eng-arpabet"` → `"eng/default"`、`"eng-arpabet-plus"` → `"eng/plus"`。
  后者是导出面携带自定义段 ID 的用例，用于对接同名方案的语言贡献（契约 §1.5）；
  常规实现只导出基本形 ID。
- 注 3：缺省恒等仅当内部 langRef 本身即合法语言贡献 ID 形态（纯 `segment`，不含路径段）
  时成立；langRef 形如 `eng/default` 含 `/` 时必须显式给出映射，否则内部子资源标识将
  泄出模块边界（契约 §2.1 的暴露面边界）。

`exports.languages` 取映射表的键集合（即暴露面）。`languages` 列表与 `bundle.json`
内部 `languages`（内部 langRef 全集）无须逐项相同——差集正是内部映射所吸收的差异。

bundle 目录内约定文件：

- `bundle.json`：**必填**。
  - 必填键：`bundle_version`、`languages`（合法 langRef 全集，如 `"eng/default"`、
    `"deu/marzipan"`）；
  - `files` 映射逻辑名 → 模型文件名，运行时打开 `encoder`、`decoder_step_init`、
    `decoder_step` 三个逻辑名（`decoder` 为导出侧保留，运行时忽略）。
- `vocabulary.json`：**必填**。
  - `symbols`（按 `{lang}/{variant}/{symbol}` 前缀组织，此处 `variant` 指 langRef 的
    资源变体段，如 `eng/default` 的 `default`，非模块三元组的 `variant`），及特殊符号表
    `global_symbols`（可选，缺失时 unk/pad/bos/eos 按约定索引 0..3 缺省）；
  - 不单独携带版本：与 bundle 同一发布单元、同一包身份，格式演进随
    `bundle_version`（解释器按 `bundle_version` 一并校验）。

内部 langRef 生效顺序：

1. 调用方 `languageId` 输入（经 `languageMap` 映射）；
2. `inference.default_language`（暴露层 ID，同样经 `languageMap` 映射）。

边界处理：

- 两级皆缺省时 → 加载期失败并诊断（模块缺省语言由 `default_language` 唯一承担，
  不设内建默认）；
- `languageId` 经映射查无内部项的 → 该词以 `error` 产出（与契约 §2.4 同界，
  不得静默改用其他语言）；
- `default_language` 经映射查无内部项的 → 属包声明与 bundle 不一致的包级错误，
  加载期失败并诊断。

## 6. S2P 变体

| variant | configuration | description |
| :--: | :-- | :-- |
| dict | `file`（**必选**，path）：TSV，每行 `发音\t音素1 音素2 …` | 词典整体查表；未命中返回空音素序列 |
| direct | 无 | 按 ASCII 空格把发音拆成音素列表（注 1） |
| mapping | `file`（**必选**，path）：TSV，每行 `原音素\t目标音素` | 逐音素替换；未列入表的音素按原样透传 |
| lua | `file`（**必选**，path）：Lua 脚本（LuaJIT 执行环境），须定义全局函数 `s2p(发音) → list<string>` | 脚本转换 |

- 注 1：连续及首尾空格产生的空段丢弃（不产出空音素）；制表符不视为分隔符。

## 7. Onset 变体

| variant | configuration | description |
| :--: | :-- | :-- |
| rule | `file`（**必选**，path）：JSON 规则定义 | 模式匹配标记 |
| lua | `file`（**必选**，path）：Lua 脚本，须导出 `markonset`，返回与输入等长的布尔表 | 脚本标记 |

rule 文件结构：

```json
{
    "phonemeTypes": { "b": "consonant", "a": "vowel" },
    "rules": [ { "pattern": ["vowel"], "onsets": [0] } ]
}
```

- `phonemeTypes`：非空 object，音素 → 自定义类型名（类型名不得为保留词 `*`）。
- `rules`：`pattern` 为音素或类型（含通配 `"*"`）组成的序列，`onsets` 给出匹配
  序列中处于 onset 位置的下标；匹配按最长、最特异者优先。
- 未登记于 `phonemeTypes` 的音素不匹配任何类型段，仅可匹配字面音素段与通配
  `"*"`；输入中未被任何规则覆盖的位置，`onsets` 对应位置输出 `false`
  （整条无匹配时输出全 `false`）。

S2P 与 Onset 变体的 `configuration` 不设 `formatVersion`：键汇极简，以「遇未知键
即加载失败」守住演进——声明中出现本表未列出的键即加载失败，防拼写错误静默生效
（与 §4 algo-cpp-pinyin 同一约定）。
