# API 变更说明（2026-08-25：本地化透传——Runtime 只做 map 直取，匹配上交前端）

> 范围：`localization/passthrough-keys` 分支相对 refactor 基线（`3994685`）的全部改动，压缩为两笔提交——
> `217862d`（refactor(core,ds-bank,ds-session)!：透传化主体）与本提交（docs(modules)：文档与本文档）。
> 规范依据：`docs/ds-spec-2.4.md` §多语言文本（L284-286：Runtime 忠实透传完整 map，不解释键、不验证语法、
> 不做大小写折叠/canonicalization/fallback/Lookup，不接收语言偏好）。
> 决策背景：`docs/plans/localization-passthrough-keys.md`。
> **取代关系**：`docs/api-changes-2026-08-22.md` §三 描述的「`text(locale)` = RFC 4647 Lookup」语义自本波
> 起作废（该文档为历史日志，不回改）；其 API 表、第五节多音修复、解析双档等其余结论不受影响。
> 断言示例：`unittests/Core/tst_display_text.cpp`、`unittests/Core/tst_display_path.cpp`、
> `domains/ds-bank/unittests/tst_localized_name.cpp`。

## 一、变更总览

- `srt::core::DisplayText` / `DisplayPath` 从「内置 RFC 4647 Lookup 匹配」改为「**忠实透传完整翻译
  map + 精确直取**」：语言匹配（候选键生成、截断、大小写归并、何时取默认文本）整体上交前端。
- 存量包中的 POSIX 写法键（`zh_CN`）从「Runtime 侧惰性不可达」变为**普通的不透明键**——Runtime 原样
  保留并可按原样拼写精确取得；能否被语言偏好命中完全取决于前端自己的归一化策略。
- `PackageValidator` 对 POSIX 键由 Error 降为 Warning（引导迁移 BCP 47）；缺 `_` 默认项保持 Error。
- 包指纹改按 `locales()` 逐键直取：POSIX 键的译文从此参与变更检测，旧「已知限制」消解。

## 二、API 变更与行为变化总表

### 接口变更（编译期可见）

| 模块 | API | 变更 |
|---|---|---|
| core | `DisplayText::text(const std::string_view &)` | 返回 `const std::string &`（内置 Lookup、兜底默认文本）→ **`const std::string *text(key)`**（精确直取、区分大小写、缺键 `nullptr`、**不回退 `_`**） |
| core | `DisplayPath::path(const std::string_view &)` | 同上：`const std::filesystem::path &` → **`const std::filesystem::path *path(key)`** |
| core | `DisplayPath::locales()` | **新增**，与 `DisplayText::locales()` 同契约（此前没有） |
| core | `DisplayText/DisplayPath`（defaultText + map）构造函数 | map 里混入的 `_` 键**防御性忽略**（默认项只来自 defaultText 参数） |
| ds-bank | `PackageValidator` | POSIX 键（含 `_`）Error → **Warning**（附 BCP 47 改写建议）；缺 `_` 仍为 Error |

不变项：无参 `text()`/`path()`（直读 `_` 默认项，不构成匹配）、`isEmpty()`、`locales()`（`_` 以外全部键、
原样拼写、`std::map` 确定性序、borrowed view 生命周期随对象）、`fromJsonValue`/`fromJsonValueTolerant`
两条解析路径（解析容错 ≠ 匹配，签名与语义均未变）。

### 行为变化（对既有调用方）

- **`text(key)` 三不**：不做子标签截断（`text("zh-Hans-CN")` 不再命中 `"zh-Hans"` 键）、不做大小写折叠
  （`text("ZH-hANS")` 不命中 `"zh-Hans"`）、不做分隔符归一化（`text("zh_CN")` 与 `text("zh-CN")` 互不命中）。
- **`text("")` 从「返回默认文本」变为 `nullptr`**；取默认文本请用无参 `text()`。
- **缺键 vs 空值可区分**：键存在但值为空串返回非空指针；旧调用方把返回值当默认文本兜底用的逻辑
  必须自行实现回退链（这正是返回指针而非引用的原因）。
- **指纹（ds-session）**：`SingerSnapshot::name` 的指纹序列化默认文本 + 逐 `(key, 译文)` 对，键经
  `text(key)` 原样直取，POSIX 键译文同样覆盖——任何翻译的编辑都触发包 changed；UI 显示语言依旧
  不参与指纹。**仍然成立**的边界：进指纹的只有歌手名，语言名/音色名的翻译不参与（沿用 08-22 波）。
- **解析行为不变**：`fromJsonValue` 严格（缺 `_` 报错）、`fromJsonValueTolerant` 宽松（缺 `_` 按
  `default` → `en` → 首个字符串条目选默认文本，全部翻译保留）。
- **C ABI 不受影响**：`include/synthrt/C` 不导出这些接口，本轮零改动。

## 三、透传模型语义（数据侧必读）

以 `docs/ds-spec-2.4.md` §多语言文本 为准，要点：

- 多语言字段值：**字符串**＝只有默认文本的简写；**对象**必须含字符串型 `"_"` 项，其余键是内容作者
  与前端约定的**语言代码**（推荐 BCP 47，**Runtime 不验证**）。
- 键对 Runtime **不透明且区分大小写**：`zh-CN` 与 `ZH-cn` 是两个不同的键；除 JSON 禁止重复键外没有任何
  归一化。Runtime API 必须允许前端取得完整 map（`locales()`）并忠实返回原样键（`text(key)`）。
- **前端的责任**：如何以用户语言偏好生成候选键序列、是否合并大小写/分隔符差异、何时回退 `_`
  （`text()`），全部由前端决定。Runtime 也不接收语言偏好参数替前端做选择（因此带参 `text()` 不再
  存在 locale 语义）。
- 多语言路径同规则；且 spec 规定每个本地化的路径值必须**各自相对其声明文件所在目录解析**后才能
  进入 API 面（`DisplayPath` 是配套类型，目前仓内尚无消费方，一致性改造先行）。

## 四、下游接入指南

### 宿主（ds-editor-lite）

1. **编译断点即迁移向导**：`name().text(locale)` 等旧调用因返回类型变为指针而编译报错，全部需改为
   「`locales()` + 自有匹配 → `text(key)` 精确直取 → 落空取 `text()`」。
2. 候选键建议取 `QLocale::uiLanguages()`（最完整条目在首），不要再用 `QLocale::name()`（POSIX 拼写
   只会与 POSIX 键精确相等）；匹配语义（截断/大小写/POSIX 归一化）由宿主自行选择实现——lite 的落地
   为 vendored `src/3rdparty/icu-wrapper`（ICU `bestMatch` 归一化候选与键，命中后按**原样拼写**取
   `text(key)`），UI 显示语言切换对已缓存对象重新取词即可，**无需重扫**（指纹不含显示语言）。
3. 老包 POSIX 键（`zh_CN`）数据面零改动即可恢复显示：Runtime 原样保留，翻转完全发生在前端匹配阶段。
4. 无语言上下文（日志/导出/比较）用 `.text()`；空判定 `.isEmpty()`；构造/赋值仍可直接传
   `std::string`/字面量（作为默认文本，翻译表不受影响）。

### 工具与自解析方

- 仓内 `tools/dsinfer-cli`、`tools/dspk-pack-cli` 只消费默认文本（`.text()`），本波零改动。
- 自己解析 JSON 的规则不变：写入/校验用严格 `fromJsonValue`，扫存量不可信包用 `fromJsonValueTolerant`。

### G2P/SVS 插件作者

- 若曾依赖 `text(locale)` 的 Lookup 行为：迁移为自有候选链 + `text(key)`；仓内 `SingerContrib`/
  `InferenceContrib`/`PackageParser` 只做解析与透传，无 locale 概念可参考调整后的注释。

### 包/数据作者

- 语言键推荐 BCP 47（`zh-CN`）：下游前端普遍按 BCP 47 口径归一化匹配，命中面最广。
- POSIX 写法（`zh_CN`）是**合法数据**，不再被 Validator 判 Error，但只能被知道该拼写（或做主分隔符
  归一化）的前端命中；Validator 会以 Warning 建议改写为 BCP 47。
- 缺 `_` 默认项仍是 Error（spec 必选）；对象形态每个值必须是字符串。

## 五、有意不兼容的设计取舍（勿想当然）

- Runtime **不做**任何匹配（Lookup/折叠/分隔符归一化/脚本推断/回退），这些是前端自由度的核心，
  不得因"方便"加回 Runtime 层（那会同时违反 spec 并重新冻结前端的匹配选择）。
- `text("")`/`text("_")` 一律为 `nullptr`：`_` 是默认项的占位语法，不是可枚举键。
- 前端各自决定匹配口径意味着**不同前端的命中集合可以不同**（lite 归一化 POSIX 分隔符与大小写，
  另一个极简前端可以只做精确直取）——这是 spec 明言的「与消费该 map 的前端约定一致」，不是缺陷。
- 指纹仍只覆盖歌手名翻译；语言名/音色名翻译变更不触发 changed（沿用 08-22 波口径，合规数据下够用）。
- 文档归口：`docs/api-changes-2026-08-22.md` §三 的 Lookup 描述与 `docs/modules/*.md` 旧匹配段
  均以本波文档为准（后者已同步改写，前者保留为历史）。

## 六、本项目日志（提交级）

**`217862d` refactor(core,ds-bank,ds-session)!: localization pass-through — opaque keys, exact lookup**
（由开发期 P0/P1/P2 三笔阶段提交压缩而成，另并入 6 处注释漂移修复）

- core：`DisplayText::text(locale)` → `const std::string *text(key)` 精确直取；删两处
  `tagEqualsIgnoreCase` 副本与 POSIX 键惰性跳过；构造函数 `_` 守卫。`DisplayPath` 同构改造
  （`path(key)` 指针化 + 新增 `locales()`），补独立单测。
- ds-bank：Validator POSIX 键 Error→Warning（缺 `_` 保持 Error）；`tst_localized_name.cpp` 改写为
  「一次解析 → `locales()` + `text(key)` 组合断言」并新增不透明键用例；`tst_package_parser_complex.cpp`
  断言适配（POSIX 键无 Error、Warning 引导 BCP 47）。
- ds-session：`fingerprintDisplayText` 逐 `locales()` 键直取（POSIX 键译文进指纹）。
- svs/g2p/Module 等波及面注释统一改写为透传契约。
- 验证（方案实施记录）：`synthrt-unittest-core-runtime` `[core][displaytext],[core][displaypath]`
  126 assertions / 11 cases 全绿；`tst-ds-bank` 148 cases / 741 assertions 全绿；ds-session
  `snapshot`/`snapshot-ensure`/`snapshot-query` 全绿（指纹用例逐位持平）。
- lite 联动（2026-08-24 记录）：vcpkg setdir 本地源构建成功，lite Release 全量构建通过，
  `TestLocalizedText` 16/16 全绿（覆盖 zh-Hans-CN→zh-Hans 候选、POSIX 键命中、zh-Hant/zh-TW
  互不串扰边界）。

**本提交 docs(modules): rewrite localization docs to the pass-through model**（由 P3 文档提交压缩而成）

- `docs/modules/core.md` / `ds-bank.md` / `ds-session.md` 匹配语义段改写为透传模型；`locales()`
  返回类型纠正为 `stdc::array_view`；ds-session 删除已被消解的指纹已知限制段。
- 新增本文档；旧方案 `docs/plans/localization-displaytext-bcp47.md` 已标注 SUPERSEDED（K2-K4 的
  「双实现收口 / locale 管线移除 / 解析不拍扁」等结论仍然有效）。
