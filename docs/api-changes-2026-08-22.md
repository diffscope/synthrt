# API 变更说明（2026-08-22：本地化收口 DisplayText/BCP 47，词典多音候选修复）

> 范围：2026-08-22 提交至 `refactor` 分支的三笔功能提交——
> `7096bb5`（core：DisplayText 升级 + g2p 收敛）、`0f18203`（ds-bank/ds-session：全翻译保留 + 校验）、
> `5abbb48`（g2p：多音词归并 + candidates 修复 + BOM 剥离）。
> 另有 `f3e613a` 为纯测试/CI 修复，无 API 影响。
>
> 规范依据：`docs/ds-spec-2.4.md` §多语言文本。决策背景：`docs/plans/localization-displaytext-bcp47.md`、
> `docs/plans/multipron-dict-variants.md`。断言示例：`unittests/Core/tst_display_text.cpp`、
> `domains/ds-bank/unittests/tst_localized_name.cpp`。

## 一、变更总览

- 多语言文本全库收敛为唯一类型 **`srt::core::DisplayText`**，语言标签采用 **BCP 47**，
  匹配采用 **RFC 4647 Lookup**；`srt::g2p::DisplayText` 删除。
- ds-bank/ds-session 的所有人读字段从"解析期拍扁成 `std::string`"改为"**保留全部翻译的
  `DisplayText`**"。
- 英语词典 8779 条 CMU 式多音变体从不可达变为可达，`word.candidates` 语义缺陷一并修正；
  附带修复 6 个语言词典的 BOM 加载缺陷。

## 二、API 变更与行为变化总表

### 接口变更（编译期可见）

| 模块 | API | 变更 |
|---|---|---|
| core | `ModuleSpec::name()`、`configurationDisplayName()` | `std::string` → `DisplayText`（后者无声明时兜底为配置键本身） |
| core | `DisplayText` | 新增 `fromJsonValueTolerant()`、`locales()`、`const char*` 构造/赋值重载；**无**字符串 `operator==`，比较请取 `.text()` |
| core | `DisplayPath` | 此前仅有声明，本轮补齐实现（多语言路径版 Lookup）；**目前尚无调用方** |
| g2p | `srt::g2p::DisplayText` | **整体删除**（头文件+实现），改用 `srt::core::DisplayText`；其私有 API（`set()/defaultText()`/JsonValue 构造与赋值）不迁移 |
| g2p | `Package::description()/vendor()/copyright()` | `g2p::DisplayText` → `core::DisplayText` |
| g2p | `PhonemeDict` | 新增 `lookupAll(key)`（返回该词全部发音，零拷贝视图）；加载时归并 `word(n)` 变体、剥离 UTF-8 BOM |
| ds-bank | `PackageManifest::{name, description, author, license}`、`SingerManifest::name`、`LanguageInfo::name`、`SpeakerInfo::name`、`SingerSnapshot::name` 及对应 setter/构造器 | `std::string` → `const srt::core::DisplayText &`（快照为值字段） |
| ds-bank/session | `PackageParser::setDisplayLocale`、`VoicebankScanner::setDisplayLocale`、`VoicebankSession::setDisplayLocale` | **全部删除，无 no-op 兼容层**，调用点编译即报错 |

### 行为变化（接口未动、语义改变）

- **`DisplayText::text(locale)` 匹配语义升级**：改动前是精确、大小写敏感的键匹配；现在是
  RFC 4647 Lookup（逐段右截 + 大小写不敏感）。已在用 `InferenceSpec/SingerSpec::name()` 的
  调用方取词结果可能变化（详见第三节的匹配规则）。
- **desc.json 许可字段只认 `copyright`**（读入 `PackageManifest::license()`）；历史写法
  `license` 不再读取，`PackageValidator` 按 extra key 出 Warning 提示迁移。
- **校验器新增 Error 级合规检查**：多语言对象缺 `"_"` 默认项 → Error；非 `"_"` 键含下划线
  （POSIX 写法如 `zh_CN`）→ Error 并提示改写。扫描加载端仍宽松（存量包不整包失败）。
- **快照指纹**：`SingerSnapshot::name` 的全部翻译（默认文本 + 各 `tag=text`）参与指纹，
  翻译内容变更会触发包 changed；**UI 显示语言不再参与指纹**（解析结果与 locale 无关）。
  注意边界：进指纹的只有歌手名，语言名/音色名的翻译不参与。
- **C ABI 不受影响**：`include/synthrt/C` 不导出这些字段相关接口，本轮零改动。

## 三、语言规范标准（数据侧必读）

以 `docs/ds-spec-2.4.md` §多语言文本 为准，要点：

- 多语言字段值：**字符串**＝只有默认文本的简写；**对象**形态必须含 `"_"`（全部未命中时的取值），
  其余每个键是 **BCP 47** 语言标签（`en`、`zh-CN`、`zh-Hant-TW`……），值必须是字符串。
  分隔符严格为 **`-`**；POSIX 写法 `zh_CN` 不是合法标签。
- 匹配（`text(locale)`）＝ **RFC 4647 Lookup**：整标签先试，不命中则从右端逐段去掉子标签
  （`zh-Hans-CN` → `zh-Hans` → `zh`），仍不命中取 `"_"`。匹配**不区分大小写**；
  **不做脚本推断**（偏好 `zh-TW` 不会命中 `zh-Hant` 键——旧实现里的繁简启发式已移除）。
- **POSIX 键永不命中**：含 `_` 的键（如 `zh_CN`）虽保留在翻译表中，但任何查找（包括传入
  `zh_CN` 本身）都不会命中它，只能落到 `"_"` 默认文本。**存量数据必须把 `zh_CN` 改写为
  `zh-CN`**。这是相对 2.3 的有意不兼容（决策 K4）。
- 哪些字段多语言：`desc.json` 的 `vendor`/`copyright`/`description`、模块声明文件的 `name`；
  声库清单中 singer/language/speaker 的 `name`（本仓实现，同规则）。多语言字段的值为文件
  路径时，每个本地化值各自相对声明文件所在目录解析（spec 规定；`DisplayPath` 是配套类型）。
- 解析有两档：**严格** `DisplayText::fromJsonValue`（不合规返回错误）与**宽松**
  `fromJsonValueTolerant`（永不失败；缺 `_` 时按 `default` → `en` → 首个字符串条目选默认文本，
  即旧的无 locale 行为）。仓内扫描声库/模块清单均走宽松档，校验器走严格检查。

## 四、下游接入指南

### 宿主（ds-editor-lite）

1. 删除 `setDisplayLocale` 调用和"`languageChanged` → 重扫声库"联动。扫描结果（快照/清单）中的
   多语言字段自承全部翻译，**切 UI 语言时对已缓存对象直接重新取词即可，无需 `refresh()`**；
   指纹也不再随显示语言变化，不会触发缓存重建。
2. 取词偏好标签传 **BCP 47**：Qt 侧用 `QLocale::bcp47Name()`，不要再用 `QLocale::name()`
   （`zh_CN` 只会命中默认文本）。
3. 可取词的对象：`SingerSnapshot::name` 及快照内完整对象 `languageInfos[i].name` /
   `speakerInfos[i].name`、`PackageManifest` 的四个字段、模块侧 `InferenceSpec/SingerSpec/ModuleSpec::name()`
   与 `configurationDisplayName(configKey)`。
4. 无语言上下文（日志/导出）用 `.text()`；空判定 `.isEmpty()`；比较一律 `.text() == "..."`。
   setter/构造仍可直接传 `std::string` 或字面量（作为默认文本，翻译表保持不变）。

### 工具与自解析方

- 仓内迁移样例：`tools/dsinfer-cli`、`tools/dspk-pack-cli`（`name` → `name().text()`）；
  `lib/SVS/{SingerContrib,InferenceContrib}.cpp`、`domains/ds-bank/lib/PackageParser.cpp`
  （统一走 `fromJsonValueTolerant`，解析器自身不再有 locale 概念）。
- 自己解析 JSON：写入/校验场景用严格 `fromJsonValue`；扫描存量不可信包用 `fromJsonValueTolerant`。

### G2P/SVS 插件作者

- `#include <synthrt/G2P/Support/DisplayText.h>` → `#include <synthrt/Core/Support/DisplayText.h>`，
  类型改写 `srt::core::DisplayText`；g2p 版私有 API 未迁移，JSON 解析改用两个 `fromJsonValue*` 静态函数。

### 包/数据作者

- 多语言对象带 `"_"`；语言键用 BCP 47（`zh-CN` 而非 `zh_CN`）；`desc.json` 许可字段用 `copyright`。
- 用 `PackageValidator` 过一遍存量包：缺 `_`、POSIX 键、旧字段名都会给出明确 Error/Warning。

## 五、英语多音候选修复（g2p，`5abbb48`）

### 5.1 多音变体从不可达变为可达

- **问题**：eng 词典（`ds_cmudict-07b.txt`，13.4 万行）含 8779 行 CMU 式变体键 `word(n)`；
  旧实现按精确字符串建索引，按词查找只能命中裸 base 一条，`(1)/(2)` 读法拿不到
  （例如 `record` 只出 `r ax k ao r d`）。
- **修复**：加载时把词尾 `(`+纯数字+`)` 的严格后缀就地剥离，同 base 的行按文件序归并为一个
  变体组（后缀号相同且发音完全一致的重复行跳过）。新增 `lookupAll(key)` 返回全部发音。
- **兼容保持**：`find/contains/operator[]` 对 base 键仍返回组首项（仓内词典 base 行均在组首，
  与历史行为一致）；带后缀精确键（如 `record(2)`）经后缀感知回退只返回该编号变体的**单条发音、
  不提供候选**（决策 D4）。受益词典：eng、deu（81 条）、ita（`è(2)`）；其余语言词典行为不变。
- 效果（eng 实测）：`record` → `pronunciation = "r ax k ao r d"`（不变），
  `candidates = ["r ax k ao r d", "r eh k er d", "r ih k ao r d"]`。

### 5.2 `word.candidates` 语义修正

- **问题**：`DictStep` 此前把 `candidates` 填成**逐个音素**的列表（`["r","ax","k",...]`），
  与仓内所有其他生产/消费方不一致——`ModelStep`、`G2pRes` 构造器（candidates 首项＝整串
  pronunciation）等都把 candidates 当**完整发音串列表**。
- **修复**：命中分支改为 `pronunciation` 取组首项、`candidates` 每项是一个变体的完整发音
  整串（音素空格分隔、无尾随空格）。私有单查接口 `lookup()` 保留兼容。
- **下游影响**：消费 `G2pRes::candidates` 的代码（编辑器候选 UI 等）在英/德/意词典路径下，
  从错误数据变为正确的可读法列表；其他语言路径语义原本就一致，不受影响。

### 5.3 词典 UTF-8 BOM 修复（附带）

deu/fra/ita/por/rus/spa 六个词典文件以 `EF BB BF` 开头，BOM 粘在首个 key 上导致该词查不到；
`PhonemeDict::load` 现读入后剥离 BOM。对 fra/ita/por/rus/spa 是纯改进，eng/cmn 等无 BOM 词典
行为不变（回归测试 G2P-052）。

## 六、有意不兼容的设计取舍（勿想当然）

- 存量 POSIX 键（`zh_CN`）**不做** `_`→`-` 兼容映射，直接失配回退默认文本（K4）；
- Lookup **不做脚本推断**（`zh-TW` 不命中 `zh-Hant`，K1/D2）；
- 指纹只覆盖歌手名翻译；语言名/音色名的翻译变更不触发 changed——合规数据下够用，属已知限制。
