# imports 泛化改造方案

> 目标：**新增一种可被 singer 导入的贡献时，synthrt 侧零改动，且旧 manifest 照常解析。**
>
> 依据 `3c962f6` 的代码。

---

## 一、最终验收

不是「代码看起来通用了」，而是这两条：

1. **wolf 端到端**：给 `singer.json` 加一条 `{"ref": ":language/cmn"}`，能解析、能拿到 `LanguageSpec`、能拿到 wolf 自己解析出的 options——**期间不改 synthrt 一行**。
2. **`SingerContrib.h` 不再包含 `InferenceContrib.h`**，`SingerContrib.cpp` 里 grep 不到 `"inference"` 字面量。

第 2 条是编译期可查的，适合放进 CI。

---

## 二、现状的耦合点，与改完后的样子

| # | 位置 | 现在 | 改完 |
|---|---|---|---|
| 1 | [SingerContrib.cpp:92](../../synthrt/lib/SVS/SingerContrib.cpp#L92) | `kInferenceCategory = "inference"` | 删除 |
| 2 | [SingerContrib.cpp:120](../../synthrt/lib/SVS/SingerContrib.cpp#L120) | 字段 `inferenceId` | `ref`（locator），旧字段作遗留拼写 |
| 3 | [SingerContrib.cpp:662](../../synthrt/lib/SVS/SingerContrib.cpp#L662) | "Fix imports"：补包名、从 dependencies 补版本 | 上移到 `SynthUnit::resolve()` |
| 4 | [SingerContrib.cpp:695](../../synthrt/lib/SVS/SingerContrib.cpp#L695) | `category("inference")->as<InferenceCategory>()` | `SU()->resolve(loc, parent())` |
| 5 | [SingerContrib.cpp:711](../../synthrt/lib/SVS/SingerContrib.cpp#L711) | `inferences.front()` 静默取第一个 | 歧义报错 |
| 6 | [SingerContrib.cpp:713](../../synthrt/lib/SVS/SingerContrib.cpp#L713) | `inference->createImportOptions(...)` | `category->createImportOptions(spec, json)` 虚函数 |
| 7 | [SingerContrib.h:56](../../synthrt/include/synthrt/SVS/SingerContrib.h#L56) | `InferenceSpec *inference()` | `ContribSpec *contribute()` |
| 8 | [SingerContrib.h:63](../../synthrt/include/synthrt/SVS/SingerContrib.h#L63) | `NO<InferenceImportOptions> options()` | `NO<NamedObject> options()` |
| 9 | SingerContrib.h 的 include | `#include <.../InferenceContrib.h>` | 删除 |

**好消息：第 7、8 条在整个仓库只有一个调用方**——[dsinfer/tools/cli/main.cpp:267](../../dsinfer/tools/cli/main.cpp#L267)。类型泄漏面比看上去小得多。

---

## 三、兼容性契约

改完之后，下面每一种写法都必须继续解析成今天同样的结果：

```json
"imports": [
  "acoustic-1",
  { "inferenceId": "pitch", "options": { "roles": ["pitch"] } },
  { "id": "bar/pitch", "inferenceId": "pitch", "version": "1.0.0.0", "options": {} }
]
```

对应关系：

| 旧写法 | 等价的新写法 |
|---|---|
| `{"inferenceId": "x"}` | `{"ref": ":inference/x"}` |
| `{"id": "p", "inferenceId": "x"}` | `{"ref": "p:inference/x"}` |
| `{"id": "p", "version": "v", "inferenceId": "x"}` | `{"ref": "p=v:inference/x"}` |

> 遗留字段与 `ref` **互斥**，同时出现报错。留到 dspk spec 下一个大版本再删。

### ⚠️ 一处需要你拍板的不兼容

裸字符串 `"acoustic-1"` 今天的含义是**「本包的 inference 中名为 acoustic-1 的那个」**——category 是写死的。

两个选项：

| | 含义 | 代价 |
|---|---|---|
| **A（建议）** | ≡ `":acoustic-1"`，category 留空由解析搜索 | 若某包里 inference 和 language 同名，从「静默选 inference」变成「报歧义」 |
| B | 保持 ≡ `":inference/acoustic-1"` | inference 永远是一等公民，泛化不彻底 |

我倾向 **A**：它才是文法的自然读法，且失败模式是**响的**（歧义错误），不是静默选错。但这确实是唯一一处行为可能变的地方，得你点头。

---

## 四、目标形状

### manifest

```json
"imports": [
  "acoustic-1",
  { "ref": ":inference/pitch",              "options": { "roles": ["pitch"] } },
  { "ref": "bar/pitch=1.0.0.0:inference/pitch", "options": {} },
  { "ref": ":language/cmn",                 "options": { "dict": "..." } }
]
```

**category 就是 import type**，不需要新词汇——`ContribLocator` 的文法本来就有这一格。

### C++

```cpp
// SynthUnit —— 文法承诺过但一直没实现的那块
/// 把相对引用补全（包名取自 from，版本取自 from 的 dependencies），再跨类别查找。
/// category 为空时搜索所有类别；命中多于一个报歧义。
Expected<ContribSpec *>      resolve(const ContribLocator &loc, const PackageRef &from) const;
std::vector<ContribSpec *>   resolveAll(const ContribLocator &loc, const PackageRef &from) const;

// ContribCategory —— 「我可以被导入」这件能力
/// 解析导入方交给本类别的 options。默认报告本类别不可被导入。
virtual Expected<NO<NamedObject>> createImportOptions(ContribSpec *spec,
                                                      const JsonValue &options) const;

// SingerImport —— 去掉具体类型
const ContribLocator &locator() const;     // 原 inferenceLocator()
ContribSpec          *contribute() const;  // 原 inference()
JsonValue             manifestOptions() const;
NO<NamedObject>       options() const;     // 原 NO<InferenceImportOptions>
```

---

## 五、分五批

每批独立可构建、可测试。顺序是有依赖的：**先把基础设施补齐，再让 category 变成数据，最后才拆类型。**

```mermaid
graph LR
    B0["<b>批 0</b><br/>SynthUnit::resolve<br/>（补文法的债）"] --> B1["<b>批 1</b><br/>imports 接受 ref<br/>（行为不变）"]
    B1 --> B2["<b>批 2</b><br/>createImportOptions<br/>上移到 category"]
    B2 --> B3["<b>批 3</b><br/>SingerImport 去类型"]
    B3 --> B4["<b>批 4</b><br/>删掉硬编码<br/>+ wolf 端到端"]
    style B0 fill:#1f6feb,color:#fff
    style B4 fill:#238636,color:#fff
```

### 批 0 — `SynthUnit::resolve()`

把两件今天散在 `SingerCategory` 里的通用逻辑收上来：

1. **相对引用补全**（现 "Fix imports"）：包名空则取自发起包；版本空则从发起包的 `dependencies` 里挑最高的；找不到则报「未在 dependencies 中声明」。
2. **跨类别查找**：category 空则遍历所有类别。

顺带修掉两个潜伏问题（见第六节）。

> **实现注意**：`ContribCategory::Impl::findContributes()` 自己拿 `su_mtx()` 的 shared_lock。
> resolve 遍历多个类别时不要反复加解锁——应当自己拿一次锁，调一个不加锁的内部版本，
> 否则跨类别的结果不是同一个快照。

**验收**：新增 `test_SynthUnit_Resolve`——category 省略能命中、歧义报错、未注册的 category 名报错**而不是崩**、依赖未声明报错。此时 `SingerCategory` 尚未改用它。

### 批 1 — imports 接受 `ref`

只动 `readSingerImport()`：认 `ref`，保留旧字段并翻译成等价 locator，裸字符串按第三节的决定处理。

**此时行为完全不变**（category 虽然进了数据，Ready 阶段仍走老路径找 inference）。这一批的意义就是**把兼容性单独验证掉**，不和别的改动混在一起。

**验收**：`test_SingerContrib` 增加一组用例，把第三节兼容性契约里的每种旧写法和它的新等价写法都解析一遍，断言 locator 相等。

### 批 2 — 钩子上移

`ContribCategory::createImportOptions()` 虚函数，默认返回 `Error::FeatureNotSupported`（「本类别不可被导入」）。`InferenceCategory` 覆盖，转发到现有的 `InferenceSpec::createImportOptions()`。

`SingerCategory` 的 Ready 改调新虚函数（但仍经由 `category("inference")`）。

**验收**：ctest 全过，行为不变。`InferenceSpec::createImportOptions()` 保留不动，dsinfer 不受影响。

### 批 3 — `SingerImport` 去掉具体类型

改 4 个签名 + 删 include + 改那**一个**调用方。

CLI 那处正好顺手改对：它现在拿 `className()` 去和 `API_CLASS` 字符串比——那正是 TODO 第 2 条抱怨的事，而它的 `ImportEntry` 表里 `API_NAME` 就在旁边闲着。这一批先只改成 `contribute()->as<InferenceSpec>()->className()` 保持等价，**不顺手改判据**（那是另一件事，见第八节）。

**验收**：`SingerContrib.h` 不含 `InferenceContrib.h`；dsinfer 与 CLI 编过；`tst_onnxdriver` 5/5。

### 批 4 — 删掉硬编码 + wolf 端到端

`SingerCategory::loadSpec(Ready)` 改成：

```cpp
for (auto &imp : importDataList) {
    auto spec = SU()->resolve(imp.locator, spec->parent());   // 不再点名 inference
    if (!spec) return spec.error();
    auto cate = SU()->category(spec.get()->category());
    auto opts = cate->createImportOptions(spec.get(), imp.manifestOptions);
    if (!opts) return opts.error();
    imp.contribute = spec.get();
    imp.options = opts.take();
}
```

删 `kInferenceCategory`、删 `#include "InferenceContrib.h"`。

**验收**：第一节那两条。wolf 侧只需实现 `LanguageCategory::createImportOptions()`。

---

## 六、必须一起修的两个潜伏问题

泛化会把它们从「今天碰不到」变成「一个写错的 manifest 就能触发」。

**一、空类别解引用。** `category("inference")` 返回 `nullptr` 时 `->as<InferenceCategory>()` 是空指针上调成员函数。今天 singer 和 inference 总是一起注册所以碰不到；**category 一旦来自 manifest，一个拼错类别名的包就能让宿主崩**。批 0 的 `resolve()` 必须返回错误而不是解引用。

**二、歧义被吞掉。** `inferences.front()` 静默取第一个，而文法注释写的是「报歧义」。category 可省略之后歧义只会更常见。

> 这条是**行为变更**：如果现有的包里真存在同名多命中，改完会从「能加载」变成「报错」。
> 批 0 落地后跑一遍手头所有包确认没有撞上，再进批 4。

---

## 七、已知取舍

**`NO<NamedObject>` 类型擦除**。消费方要 `as<InferenceImportOptions>()` 才能用。缓解：import 的 `contribute()->category()` 已经告诉调用方该往哪个类型 cast，和仓库里 `ContribSpec::as<T>()` 是同一套约定。代价真实但可控——毕竟目的就是让 synthrt 不认识这些类型。

**遗留字段要养多久**。建议留到 dspk spec 下一个大版本；在那之前 `ref` 与旧字段互斥且同时出现即报错，避免出现两种拼写打架的包。

**`resolve()` 的锁粒度**。跨类别遍历持锁时间比现在长。当前所有类别共用一把 `su_mtx`，所以不是新问题，只是被拉长了——如果将来成为热点，那是锁分片的事，不在本次。

---

## 八、不在本次范围

**两阶段加载的天花板。** singer 的 `Ready` 读 inference 的 `Initialized` 产物，靠「所有 Initialized 做完再统一 Ready」保证——**只支持一层跨贡献引用**。哪天 language 自己也要 import 东西、而 singer 的 Ready 需要 language 的 Ready 产物，两阶段就不够。

好在 import 一旦泛化，**spec 之间的依赖图就从 manifest 直接可读**，加载顺序应当由它的拓扑序决定，而不是两个写死的阶段。这是本方案自然打开的下一步，但现在不做。

**TODO 第 2 条：`class` 拆出 kind。** `class` 现在同时回答「谁能解析我」和「我是什么」。而「我是什么」其实已经存在——`API_NAME`（`"acoustic"`），只是没进 manifest。把它提成一等字段，`(kind, level)` 就是可互换性的判据，编辑器不必再拿 `class` 全字符串匹配。

这件事和 imports 泛化**正交**，可以并行推进；批 3 里 CLI 那处判据先保持等价不动，就是为了不把两件事绞在一起。

**文档对齐。** [ds-spec-2.3.md:203](../ds-spec-2.3.md#L203) 的示例对象缺 `inferenceId`，按现在的代码会被拒——文档和实现早就不一致了。改 imports 格式时一并对齐到 locator 上。
