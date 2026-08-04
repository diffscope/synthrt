# synthrt 代码审查问题清单

> 生成于 2026-08-01，基于 commit `304b275`（分支 `main`）。
> 最后回填 2026-08-04，对应分支 `fix/group-a-bugs` 的 21 个提交。
> 每修复一条就把 `[ ]` 改成 `[x]`，并在条目末尾补一行 `> 修复：<commit/说明>`。
>
> 分组顺序即建议的处理顺序：**A（确定 bug）→ B（健壮性/并发）→ E（Error 重设计）→ C（代码质量）→ D（设计，最后讨论）**。
> **E 组不在初次审查里**，是修 B2 时由用户提出、单独讨论后新增的。

## 提交对照

| SHA | 内容 | 对应条目 |
|---|---|---|
| `711b5a3` | A 组 19 项 bug 修复 | A1 A2a A4 A6–A15 |
| `51f0f99` | 适配新 stdcorelib API | 无（外部依赖升级） |
| `c6d3eb8` | `BuildAPI.cmake` → `QMBuildRepoHelpers` | 无（构建系统迁移） |
| `a55dc98` | logging 别名化到 stdcorelib | C9 |
| `e17c7bb` | `Expected` 的 const / `valueOr` 缺陷 + `[[nodiscard]]` | B2 |
| `a5e09a0` | `Error` 开放错误域 + 因果链 | 新增 E1 E2 |
| `de9dcb5` | 注释风格统一 | 无 |
| `4d222bc` | contribute 引用文法替换 + id 校验对齐 + 键名去复数 | A2b A3 A17 A18 C3 D7 |
| `1938962` | 新增 `UNO`，`NO` 瘦身 | D5（部分） |
| `1d969c5` | 一个类一个测试可执行文件 + `manual/` 目录 | D6（部分） |
| `a995ac0` | 补全 Support 测试（断言 ~90 → 384） | D6（部分） |
| `2ed248d` | 删掉 `DisplayText` 的弃用 JSON 入口 | 无 |
| `8770f00` | `DisplayPath` 测试 | D6（部分） |
| `eb25a18` | 修正类别名断言处的失实注释 | F7（注释部分） |
| `55b133e` | 类别注册表三处缺陷 | B1 F1 F3 F6 |
| `a5213e6` | `SynthUnit` 测试 | D6（部分） |
| `3920a22` | 三处零风险修复 | B8a C6 C8 |
| `e1d0acf` | 删 `ContribCategory::key()`、插件接口加 `IID` | F2 F4 F5 |
| `b26339f` | manifest 错误改用 `withContext` 链接 | E2（收尾前半） |
| `6c5dc6f` | vcpkg 子模块：带来 `StaticRegistry` | 无（外部依赖升级） |
| `008113c` | 类别注册迁到 `stdc::StaticRegistry` | F7 F8 |
| `ff71e53` | dsinfer 自己的错误域 `ds::ErrorCode` | E1（收尾） |
| `b612a00` | dsinfer 的 `Error.h` 改名 `ErrorCode.h` | E1（收尾） |
| `64206ec` | vcpkg 子模块：stdcorelib 默认日志 sink 修复 | 无（外部依赖升级） |
| `4338587` | `InferenceDriver::extension()` | 无（新特性） |
| `00ab374` | 43 处 `withContext`，说明失败发生在哪个输入 | E2（收尾后半） |
| `9b4c63d` | build helpers 收编进 qmsetup | 无（构建系统迁移） |
| `c1d6d85` | 安装 `cmake/`（`AddAutoTest`）随包发布 | 无（构建系统迁移） |
| `e71f029` | `README` / `AGENTS.md` / `docs/Status.md` | 无（文档） |

## 标记约定

| 标记 | 含义 |
|---|---|
| `[ ]` / `[x]` | 待修 / 已修 |
| 🔒 **保留** | **记录但不动手**。原因写在条目里，通常是：修法依赖对原作者意图的猜测，需先确认；或已明确决定暂不处理。 |

**🔒 的条目不要顺手改**——需要先和作者确认意图。凡是我在审查中标注"看上去原意是…"、"待确认"、"倾向…"的推测性判断，一律归入此类，只留证据不留结论。

---

## A. 确定的 bug（优先修）

### A1. contrib 初始化回滚路径会抛 `std::out_of_range`
- [x] **A1** — 已修（该分支补 `i--`）

[synthrt/lib/Core/SynthUnit.cpp:260-286](../../synthrt/lib/Core/SynthUnit.cpp#L260-L286)

category 查找失败时 `failed = true; break;` 但**没有 `i--`**（其它失败分支都有）。回滚循环 `for (; i >= 0; --i)` 于是从当前 `i` 开始，第一件事就是
`categories.at(contribute->_impl->category)` —— 查的正是那个不存在的 category，`std::map::at` 直接抛异常。

**修法**：该分支补 `i--`；或把回滚改成"只回滚已成功的下标区间"，用一个显式的 `succeeded` 计数而不是复用循环变量。

---

### A2. `ContribLocator::fromString` 把 `]` 解析进版本号
- [x] **A2a** substr 长度错误 — 已修
- [x] **A2b** `<package>[<version>]`（无 id）无法解析 — 已修（`4d222bc`）

> **修复方式与原计划不同**：没有给旧文法补一个分支，而是**整套文法被替换**（见 [D7](#d7-contriblocator-的字符串语法与官方引用语法不一致)）。
> 新文法里 `pkg=1.0.0` 本身就是合法的"只有包、没有 contrib"形式，A2b 描述的缺口不再存在。
> `PackageListConfig` 的 save/load 现在闭合，`4d222bc` 里补了 `_RoundTrip` 测试。
> 那条固化缺陷行为的断言（`fromString("a[1.2.3]") == ContribLocator()`）随整个测试文件重写而消失。

[synthrt/lib/Core/Contribute.cpp:61-62](../../synthrt/lib/Core/Contribute.cpp#L61-L62)

**a)** 确定 bug：
```cpp
result._version = stdc::VersionNumber::fromString(
    leftPart.substr(openBracket + 1, leftPart.size() - openBracket - 1));
```
长度应为 `leftPart.size() - openBracket - 2`。现在 `foo[1.0]/bar` 得到的版本串是 `"1.0]"`。
**修法**：改长度即可，无歧义。建议配 round-trip 单测。

**b)** **确认是真 bug。**（本条判定反复过两次，最终结论以此为准。）

`fromString` 对不含 `/` 的输入走 "sid only" 分支，而 `isValidLocator` 的黑名单含 `[` `]`，因此 `"a[1.2.3]"` 恒返回空定位器。

**决定性证据 —— [dsinfer/lib/Support/PackageListConfig.cpp](../../dsinfer/lib/Support/PackageListConfig.cpp) 自身的 save/load 不闭合：**

- [`save()` :145-146](../../dsinfer/lib/Support/PackageListConfig.cpp#L145-L146) 把 id 写成 `stdc::formatN("%1[%2]", id, version)`，即 `vendor/pkg[1.0.0]`；
- [`load()` :78-85](../../dsinfer/lib/Support/PackageListConfig.cpp#L78-L85) 用 `fromString` 解析，并要求结果满足
  `!package().empty() && !version().isEmpty() && id().empty()` —— **正是无 id 形式**；
- 但 `fromString` 永远返回空定位器 → 条件永不成立 → 每个条目都 `continue`。

**后果：`PackageListConfig::load()` 无论输入什么都解析出 0 个包，`save()` 写出的文件自己也读不回来。**
该类是 dsinfer 的导出公共 API（[PackageListConfig.h:89](../../dsinfer/include/dsinfer/Support/PackageListConfig.h#L89)，"Package install directory status configuration file reader/writer"），
仓库内暂无调用方，所以一直没暴露。

**修法**：在 `fromString` 中补 `<package>[<version>]`（无 `/`、无 id）的解析分支。

⚠️ **必须同时改测试**：[synthrt/tests/auto/Core/Contribute.cpp:49-52](../../synthrt/tests/auto/Core/Contribute.cpp#L49-L52)
把 `fromString("a[1.2.3]") == ContribLocator()` 断言在 "errors" 分组里。**这条断言是错的**——它固化了缺陷行为，而不是表达设计意图。

> **我的判定失误记录**：我曾据这条测试断定 A2b "非缺陷"并撤销。这是过度信任测试的结果——
> 测试可能只是把既有行为写死。`PackageListConfig` 的 save/load 才是真正的意图证据。

> 顺带记录：`a[1.2.3]/b` 这条断言在修复 A2a **之前也是通过的**，说明 `stdc::VersionNumber::fromString` 对尾部垃圾字符（`"1.2.3]"`）是宽容的，
> 掩盖了 substr 长度错误。这也意味着**现有测试无法捕获 A2a**——若要防回归，需要断言一个宽容解析也会失败的版本串。

---

### A3. `findContributes` 让文档承诺的两种定位器形式永远失效
- [x] **A3** — 已修（`4d222bc`，注释随新文法一并重写）

> 结论未变：`findContributes` 要求完整 locator 是设计如此。`4d222bc` 重写了 [Contribute.h](../../synthrt/include/synthrt/Core/Contribute.h) 的
> 文档块，把**字符串文法**与 **`find()` 的前置条件**分开陈述。
> 顺带收紧了 `findContributes`：locator 带 category 而与被查 category 不符时直接返回空，此前是忽略该信息。

[synthrt/lib/Core/Contribute.cpp:138](../../synthrt/lib/Core/Contribute.cpp#L138)

```cpp
if (loc.package().empty() || loc.version().isEmpty()) { return {}; }
```

但 [synthrt/include/synthrt/Core/Contribute.h:16-19](../../synthrt/include/synthrt/Core/Contribute.h#L16-L19) 明确声明支持三种形式：

| 形式 | 例子 | 当前行为 |
|---|---|---|
| `<package>[<version>]/<contrib>` | `foo[1.0]/bar` | ✅ 可用 |
| `<package>/<id>` | `foo/bar` | ❌ 恒返回空 |
| `<contrib>` | `bar` | ❌ 恒返回空 |

🔓 **已由官方规范澄清**（[package-specification 1.0](https://dspk.diffscope.org/docs/1.0/package-specification.html)）：

> "When a singer import refers to another package without an explicit version, tools resolve it against the available
> dependency entries for that package ID."

**版本补全发生在查找之前**，正是 [SingerContrib.cpp:656-683](../../synthrt/lib/SVS/SingerContrib.cpp#L656-L683) "Fix imports" 做的事：
在 `Initialized` 阶段把 locator 补成 "package + 精确 version"，再交给 `findContributes`。

**结论：`findContributes` 只接受完整 locator 是设计如此，代码正确。过时的是 [Contribute.h:16-19](../../synthrt/include/synthrt/Core/Contribute.h#L16-L19) 的注释**——
它描述的是 `ContribLocator` **字符串语法**支持的形式，却容易被读成 `find()` 也接受这些形式。

**修法（低风险，不改行为）**：改注释，把"字符串语法"与"`find()` 的前置条件"分开写清楚，并在 `ContribCategory::find()` 上注明要求完整 locator。

**补充佐证**：`singer-desc` 对 `version` 默认值的规定与 "Fix imports" 的实现**逐条吻合**：

> "If `package` is not specified, default to current package version, otherwise default to the
> **latest version of the package required by the `dependency` field** in the package description."

对应 [SingerContrib.cpp:661-680](../../synthrt/lib/SVS/SingerContrib.cpp#L661-L680)：package 为空 → 取当前包 id 与版本；
否则遍历 `dependencies()` 取满足 `dep.version > version` 的最大值，找不到则报 "not declared in dependencies"。
**该逻辑正确，无需改动。**

---

### A4. `pluginsDirty` 从不清除 → 每次 `plugin()` 都重扫插件目录
- [x] **A4** — 已修（`scanPlugins` 末尾 `pluginsDirty.erase(iid)`）

[synthrt/lib/Plugin/PluginFactory.cpp:46-89](../../synthrt/lib/Plugin/PluginFactory.cpp#L46-L89)、[:186](../../synthrt/lib/Plugin/PluginFactory.cpp#L186)

`scanPlugins()` 结束时没有 `pluginsDirty.erase(iid)`。于是每次 `plugin(iid, key)` 都会：
遍历目录 → 对每个文件 `fs::canonical` → 尝试 `SharedLibrary::open` → `resolve("synthrt_plugin_instance")`。
且 `plugin()` 用的是 `unique_lock` **独占**锁，所有插件查询完全串行。

**修法**：`scanPlugins` 末尾 `pluginsDirty.erase(iid)`。之后可考虑把 `plugin()` 改成"先 shared_lock 快路径查缓存，miss 才升级为 unique_lock 扫描"。

---

### A5. `JsonValue::Undefined` 是不可达状态
- [ ] 🔒 **A5** — **保留**（作者已明确：暂不处理）

[synthrt/lib/Support/JSON.cpp:718-721](../../synthrt/lib/Support/JSON.cpp#L718-L721)

```cpp
case Undefined: { json = nullptr; break; }   // 与 Null 存成同一个底层表示
```

`type()` 因此对 undefined 值返回 `Null`，`isUndefined()` **永远为 false**。
后果：[JSON.cpp:997](../../synthrt/lib/Support/JSON.cpp#L997) / [:1008](../../synthrt/lib/Support/JSON.cpp#L1008) 里 `operator[]` 取不到 key 时返回的 `undefinedValue()`，调用方无法区分"字段不存在"和"字段值是 null"。

🔒 **保留原因**：作者已决定暂不处理。可选方案（加独立标志位 / 删掉 `Undefined` 枚举改用 `contains()` 之类的 API）各有取舍，
且与 [D2](#d2-jsonvalue-的-proxy-容器方案是否值得继续) 的整体走向绑定，等 D 组一起谈。

**注意**：在此之前，代码里**不要依赖 `isUndefined()`**——它恒为 false。当前无调用方，保持这样。

---

### A6. `proxy_map::iterator` 拷贝构造解引用空 `optional`
- [x] **A6** — 已修（拷贝只复制 `it`，`ref` 留空/reset）

[synthrt/lib/Support/JSON.cpp:304-314](../../synthrt/lib/Support/JSON.cpp#L304-L314)

```cpp
iterator_base(const iterator_base &RHS) : it(RHS.it) {
    ref.emplace(RHS.ref->first, RHS.ref->second);   // RHS.ref 可能是空 optional → UB
}
```
`ref` 只在 `operator*` / `operator->` 里才 `emplace`。任何"未解引用就拷贝"的路径（典型：后置 `it++` 内部的 `auto tmp = *this;`）都是 UB。
`operator=` 同样问题。

**修法**：拷贝构造/赋值只复制 `it`，`ref` 保持空（它本来就是惰性缓存）：`iterator_base(const iterator_base &RHS) : it(RHS.it) {}`。

---

### A7. `proxy_map::erase(const_iterator)` 自我无限递归
- [x] **A7** — 已修（改走 `buf.erase`；并补上 `at()` 非 const 版定义）

[synthrt/lib/Support/JSON.cpp:427-429](../../synthrt/lib/Support/JSON.cpp#L427-L429)

```cpp
iterator erase(iterator pos)       { return buf.erase(pos.it); }   // 对
iterator erase(const_iterator pos) { return erase(pos.it); }       // 少了 buf.
```
`pos.it` 是 `JsonObject::const_iterator`，会隐式转回 `proxy_map::const_iterator` → 调用自己 → 栈溢出。

同一个类里 [`T &at(const Key&)`（非 const 版，:365）](../../synthrt/lib/Support/JSON.cpp#L365) 只声明未定义，一旦被 nlohmann 实例化就是链接错误。

**修法**：`return buf.erase(pos.it);`；补上 `at()` 非 const 版定义或删掉声明。这几处目前靠"未被实例化"侥幸没炸，**必须配单测**（见 D6）。

---

### A8. `PhonemeDict::load` 读失败后留下整表悬垂指针
- [x] **A8** — 已修（`map.clear()` 提到 `filebuf.resize()` 之前）

[dsinfer/lib/Support/PhonemeDict.cpp:65-73](../../dsinfer/lib/Support/PhonemeDict.cpp#L65-L73)

```cpp
filebuf.resize(file_size + 1);          // 可能重分配 → 旧 map 的所有 char* key 失效
if (!file.read(...)) {
    filebuf.clear();
    return false;                       // ← map 未清空，全部指向已释放内存
}
filebuf[file_size] = '\n';
map.clear();                            // 太晚了
```
之后任何 `find()` / `contains()` / `operator[]` 都是 UB。

**修法**：把 `map.clear()` 提到 `filebuf.resize()` **之前**；失败路径也保证 map 为空。

---

### A9. 异步推理的两处生命周期问题
- [x] **A9a** 输入名悬垂指针 — 已修（`SessionAsyncRunContext` 持有 `NO<SessionStartInput>` 保活）
- [x] **A9b** 回调与 Session 析构的竞争 — 已修（运行标志 + 条件变量，`close()` 等待回调完成）

> **A9b 的行为变更（需知晓）**：`close()` 现在会**阻塞**直到在途的异步推理完成。
> 这是正确语义（不能在运行中拆掉 session），但与此前"立即返回（然后可能崩）"不同。
> 需要提前中断时应先调 `Session::terminate()`，它会让 ORT 以错误状态触发回调，`close()` 随即解除阻塞。
>
> 已处理的重入场景：用户回调里调用 `close()`（"跑完就关"）会等到自己所在的线程 → 死锁。
> `waitForAsyncRun()` 通过比对 `asyncCallbackThread` 检测这种情况并直接返回。

[dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp:489-583](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L489-L583)

**a)** `ctx.inputNames.push_back(name.c_str())`（:533）存的是调用方 `sessionStartInput` 里 map key 的裸指针，而 `sessionRunAsync` 只按 `const &` 接收、**不持有**这个 `NO<>`。`RunAsync` 立即返回后调用方一旦释放输入，ORT 工作线程读到的就是悬垂指针。`ctx.outputNames` 同理。
（同步路径没问题，`Run` 是阻塞的。）

**b)** [`runAsyncCallback`（:337-370）](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L337-L370) 在 ORT 线程上无锁读写 `impl.context` / `impl.asyncContext` / `impl.sessionResult`；若 `Session` 在回调前析构就是 use-after-free。

**修法**：`SessionAsyncRunContext` 里持有一份 `srt::NO<Api::Onnx::SessionStartInput>`（保活 key 字符串），或干脆把名字深拷贝成 `std::vector<std::string>` + 另一份 `const char*` 索引表；`Session` 析构时等待/取消未完成的 async run。

---

### A10. OnnxDriver：格式串写错 + 搜索路径泄漏 + 错误信息丢失
- [x] **A10a** `formatN("%1: failed to get API instance")` 有占位符无参数 — 已修（补 `path` 实参）
- [x] **A10b** 用了 printf 风格 `%s` 而 `formatN` 认 `%1`/`%2` — 已修
- [x] **A10c** 加载失败时未恢复 DLL 搜索路径 — 已修（新增 `ScopedLibraryPath` RAII，作用域收窄到 `open()` 调用）
- [x] **A10d** `initialize` 丢掉 `impl.load()` 的详细错误 — 已修（直接透传 `Expected`）

---

### A11. 声学推理：整数除零 + 漏检一个参数 + `stop()` 空指针
- [x] **A11a** 整数除零 — 已修
- [ ] 🔒 **A11b** `satisfyMouthOpening` 算了但没检查 — **保留**
- [x] **A11c** `stop()` 不判空 — 已修（补空指针检查；**加锁问题未动**，见下）

[dsinfer/plugins/inferenceinterpreters/acoustic/AcousticInference.cpp](../../dsinfer/plugins/inferenceinterpreters/acoustic/AcousticInference.cpp)

**a)** [:220](../../dsinfer/plugins/inferenceinterpreters/acoustic/AcousticInference.cpp#L220) `intDepth = intDepth / acceleration * acceleration;`
`acceleration` 直接来自用户输入的 `acousticInput->steps`（:191），为 0 时整数除零 → 崩溃。入口处应校验 `steps >= 1`。

**b)** 🔒 **保留**：[:248](../../dsinfer/plugins/inferenceinterpreters/acoustic/AcousticInference.cpp#L248) 算出了 `satisfyMouthOpening`，
但 [:440](../../dsinfer/plugins/inferenceinterpreters/acoustic/AcousticInference.cpp#L440) 的校验块只查 energy / breathiness / voicing / tension。

现象确定：`satisfyMouthOpening` 是**只写不读**的变量。但**它属于哪一类参数是猜测**——
`gender` / `velocity` 同样只写不读，而它们在 [:278-301](../../dsinfer/plugins/inferenceinterpreters/acoustic/AcousticInference.cpp#L278-L301) 有"缺失时填默认值"的兜底逻辑，
`mouth_opening` 两样都没有。所以可能是：①漏加进必需列表；②漏加兜底默认值；③有意不校验。
[docs/dsinfer-level-1-draft.md](../../docs/dsinfer-level-1-draft.md) 的 acoustic 变量表里**根本没有 `mouth_opening`**，无从对照。等确认。

**c)** [:524-534](../../dsinfer/plugins/inferenceinterpreters/acoustic/AcousticInference.cpp#L524-L534) `stop()` 既不加锁也不判空，`initialize()` 之前调用直接空指针解引用。

---

### A12. `reserve` 复制粘贴笔误
- [x] **A12** — 已修

[synthrt/lib/SVS/SingerContrib.cpp:549](../../synthrt/lib/SVS/SingerContrib.cpp#L549)
[synthrt/lib/SVS/InferenceContrib.cpp:335](../../synthrt/lib/SVS/InferenceContrib.cpp#L335)

两处都是 `res.reserve(res.size());`，应为 `temp.size()`。无害，但是明显笔误，顺手改。

---

### A13. `Algorithm.h` 的边界与未定义行为
- [x] **A13a** `interpolate` 空输入 — 已修（提前返回空）
- [x] **A13b** `interpolate` 不校验 values/points 长度一致 — 已修
- [x] **A13c** `arange` step==0 — 已修（含负 step 方向判断与 `isfinite` 校验）
- [x] **A13d** 缺 `#include <limits>` — 已修（顺带补 `<type_traits>`）

连带修复：`resample()` 在 `interpolate()` 返回空时会对空 vector 调 `back()`。这是 A13a 修复**引入**的新可达路径，
已在同一处加了空检查（[Algorithm.h](../../dsinfer/util/inferutil/include/inferutil/Algorithm.h) 的 `resample`）。

[dsinfer/util/inferutil/include/inferutil/Algorithm.h](../../dsinfer/util/inferutil/include/inferutil/Algorithm.h)

- [:46-48](../../dsinfer/util/inferutil/include/inferutil/Algorithm.h#L46-L48) `referencePoints.front()` / `.back()`，view 为空时 UB。
- [:56/:60](../../dsinfer/util/inferutil/include/inferutil/Algorithm.h#L56) 按 `referencePoints` 的下标去索引 `referenceValues`，两者长度不一致时越界。
- [:88](../../dsinfer/util/inferutil/include/inferutil/Algorithm.h#L88) `arange` 在 `step == 0` 时 `ceil(inf)` 再 `static_cast<size_t>` → UB；负 step 且 `stop < start` 的组合也没处理对。
- [:37](../../dsinfer/util/inferutil/include/inferutil/Algorithm.h#L37) 用了 `std::numeric_limits` 但只 include 了 `<algorithm> <cstdint> <cmath> <vector>`，缺 `<limits>`（现在靠传递包含侥幸编过）。

---

### A14. `PhonemeDict::load` 未校验 `tellg()` 失败
- [x] **A14** — 已修（`file_size < 0` 时提前返回 `errc::io_error`，且早于 `map.clear()`，加载失败不破坏已有词典）

[dsinfer/lib/Support/PhonemeDict.cpp:62-66](../../dsinfer/lib/Support/PhonemeDict.cpp#L62-L66)

```cpp
std::streamsize file_size = file.tellg();   // 失败时返回 -1
filebuf.resize(file_size + 1);              // → resize(0)
if (!file.read(filebuf.data(), file_size))  // → read(nullptr, -1)
```
`tellg()` 对不可 seek 的流（管道、某些设备）返回 -1。之后 `filebuf.data()` 是 `nullptr`，读长度为负。
修法无歧义：`if (file_size < 0) { ...error...; return false; }`。
**与 A8 是同一个函数但属不同缺陷，未一并修改**，避免把两件事混进一次改动。

---

### A15. 移动后的 `Session` 析构会解引用空 `_impl`
- [x] **A15** — 已修（析构与 `close()` 判空；移动语义契约写进 [Session.h](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.h)）

[dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp:593-603](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L593-L603)

```cpp
Session::Session(Session &&other) noexcept { std::swap(_impl, other._impl); }
```
`this->_impl` 构造时为 null，swap 后 **`other._impl` 变成 null**。随后 `other` 析构 → `close()` → `__stdc_impl_t` 解引用空指针。
`operator=` 同理。移动构造/赋值是 public API（[Session.h:29-30](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.h#L29-L30)），当前无调用方，所以尚未暴露。
修法：`close()` / 析构里判空，或改成让移动后的对象保持一个空 Impl。

---

### A17. 拒绝 spec 允许的字符串形式 `imports` 条目
- [x] **A17** — 已修（`4d222bc`）⚠️ **是 `e5e25a6` 引入的回归，不是遗留问题**

> **按 spec 而非按旧实现修**：`val.isString()` 时整个字符串作为 `inferenceId`，package/version 留空，
> 交给 "Fix imports" 补全。没有恢复旧代码的 `ContribLocator::fromString`——那会连带接受定位器串，比 spec 宽松。
> 规范作者已确认该做法（用户 2026-08-02 转达）。

[synthrt/lib/SVS/SingerContrib.cpp:95-100](../../synthrt/lib/SVS/SingerContrib.cpp#L95-L100)

```cpp
static bool readSingerImport(const JsonValue &val, ...) {
    if (!val.isObject()) {
        *errorMessage = R"(invalid data type)";
        return false;          // ← 字符串形式被一律拒绝
    }
```

[singer-desc 1.0](https://dspk.diffscope.org/docs/1.0/singer-desc.html) 规定 `imports` 的每个条目是
`oneOf [ genericIdentifier, object ]`，字符串形式的语义为：

> "Identifier of an inference from the same package. Equivalent to an object with only the `id` field specified
> and default values for other fields."

即 `"imports": ["acoustic", "vocoder"]` 是**合法清单**，当前实现会直接报错拒绝加载整个歌手。

**溯源（`git show e5e25a6 -- synthrt/lib/SVS/SingerContrib.cpp`）**：该分支是被 `e5e25a6 "Sync desc file json schema (#1)"` **删除**的：

```diff
 static bool readSingerImport(const JsonValue &val, ...) {
-    if (val.isString()) {
-        auto inference = ContribLocator::fromString(val.toString());
-        if (inference.id().empty()) { *errorMessage = R"(invalid id)"; return false; }
-        SingerImportData res;
-        res.inferenceLocator = inference;
-        *out = std::move(res);
-        return true;
-    }
     if (!val.isObject()) { *errorMessage = R"(invalid data type)"; return false; }
```

`e5e25a6` 已在 HEAD 的历史中（`e5e25a6` → `0182020` → `dba99ab` → `304b275`），
所以**当前代码确实不支持字符串形式**，且这是该次提交造成的回归。

**修法（注意：不是简单 revert）**：旧实现用 `ContribLocator::fromString(...)` 解析，会连带接受 `pkg/inf`、`pkg[1.0]/inf` 这类**定位器串**，
比 spec 更宽松。spec 规定字符串就是一个 `genericIdentifier`（`^[A-Za-z0-9_-]+$`）。
正确做法是：`val.isString()` 时把整个字符串当作 **`inferenceId`**，package/version 留空，
随后由 "Fix imports" 补成当前包及其版本——与对象形式省略 `id`/`version` 时走同一条路径。

**原始 schema 结构**（`docs/1.0/singer-desc.html`，json-schema-for-humans 渲染）：

```
imports (Required, array) — 每个 item 为 One of:
  ├─ genericIdentifier : string, 正则 ^[A-Za-z0-9_-]+$
  │    "Identifier of an inference from the same package.
  │     Equivalent to an object with only the `id` field specified and default values for other fields."
  └─ object (No Additional Properties):
       id          : string, 正则 ^[A-Za-z0-9_-]+(?:/[A-Za-z0-9_-]+)*$
                     "Identifier of the package to import the inference from. Default to current package ID."
       inferenceId : string (Required), 正则 ^[A-Za-z0-9_-]+$
                     "Identifier of the inference within the imported package"
       options     : object
       version     : string, 正则 ^(0|[1-9][0-9]*)(?:\.(0|[1-9][0-9]*)){0,3}$
                     "If `package` is not specified, default to current package version, otherwise
                      default to the latest version of the package required by the `dependency` field"
```

⚠️ **规范文本里有两处过时措辞，疑似 `id` → `inferenceId` 改名时的遗留**（值得反馈给规范作者）：

1. 字符串分支说 "Equivalent to an object with only the **`id`** field specified" ——
   但在当前对象定义里 `id` 是**包** id，而 `inferenceId` 才是必填项。
   "只指定 id" 在新命名下讲不通（会缺 Required 的 `inferenceId`）。该句应指 **`inferenceId`**。
2. `version` 的说明写 "If **`package`** is not specified" —— 但对象里没有名为 `package` 的字段，该字段现名为 `id`。

这两处都指向同一件事：**规范 prose 停留在旧命名，而对象定义已改新命名**。
`e5e25a6` 在代码侧做的正是这次改名（`obj.find("id")` → `obj.find("inferenceId")`，并让 `id` 改指包）。
合理推测：改名过程中，代码侧的字符串分支被顺手删掉了，而规范侧的字符串分支保留至今——两边朝相反方向漂移。

---

### A18. 歌手/推理 id 的校验比 spec 宽松得多
- [x] **A18** — 已修（`4d222bc`）**破坏性变更，用户认可**（仓库处于内测期，无需过渡期）

spec 对两者的 id 都规定了严格正则：

- [singer-desc](https://dspk.diffscope.org/docs/1.0/singer-desc.html)：`id` — "Singer identifier. Must be unique within the package." 正则 `^[A-Za-z0-9_-]+$`
- [inference-desc](https://dspk.diffscope.org/docs/1.0/inference-desc.html)：`id` — 同样是 `^[A-Za-z0-9_-]+$`

而代码用的是 `ContribLocator::isValidLocator`（[Contribute.cpp:80-100](../../synthrt/lib/Core/Contribute.cpp#L80-L100)），
只黑名单了 `/ \ [ ] : ; ' "` 八个字符 —— 空格、点号、中文、控制字符全部放行：

- [SingerContrib.cpp:261](../../synthrt/lib/SVS/SingerContrib.cpp#L261) 歌手 id
- [SingerContrib.cpp:118](../../synthrt/lib/SVS/SingerContrib.cpp#L118) import 的 `inferenceId`
- [InferenceContrib.cpp:160](../../synthrt/lib/SVS/InferenceContrib.cpp#L160) 推理 id

包 id 反而是对的：`isValidPackageIdentifier` 的 `^[A-Za-z0-9_-]+(?:/[A-Za-z0-9_-]+)*$` 与
[package-desc](https://dspk.diffscope.org/docs/1.0/package-desc.html) 完全一致。

**溯源**：`e5e25a6` **新增**了 `isValidPackageIdentifier` 并把它用在 imports 的**包 id** 上（该提交 diff 第 43、127 行），
但歌手 id / 推理 id 的 `isValidLocator` 校验**原样保留、未改动**（diff 第 229→290 行只是位置移动，检查本身不变；
InferenceContrib 第 114→175 行同理）。
所以 A18 是**先于 `e5e25a6` 就存在、且未被它覆盖**的遗留问题——那次同步只对齐了包 id。

**这条同时解决了 [C3](#c-代码质量--可维护性可批量处理)**（两套校验规则冲突）：spec 说了算，id 用严格正则，`isValidLocator` 只应用于解析定位器字符串。

⚠️ **收紧校验是破坏性变更**：现有那些 id 含空格/点号的包会从"能加载"变成"加载失败"。

> **实现（`4d222bc`）**：黑名单 `isValidLocator` 整个删除，换成两个白名单函数：
> `isValidSegment`（`^[A-Za-z0-9_-]+$`）与 `isValidPackageId`（段以 `/` 连接）。
> 歌手 id、推理 id、import 的 `inferenceId` 全部改用 `isValidSegment`。
> **按"直接拒绝"实现，无过渡期警告**——用户裁定内测期不必兼容存量。
> `ContribCategory` 的构造函数也加了断言，要求 category 名本身是合法 segment（否则拼出来的定位器无法解析）。

---

### A16. 构建目录的头文件依赖追踪是坏的（环境问题，非代码）
- [ ] **A16**

[build/Debug/CMakeFiles/rules.ninja:17](../../build/Debug/CMakeFiles/rules.ninja#L17) 里
`msvc_deps_prefix` 存的是 **UTF-8** 编码的「注意: 包含文件:」，而 cl.exe 在 GBK 控制台下输出的是 **GBK** 字节。
两者不匹配，导致：

1. `/showIncludes` 的输出无法被 ninja 识别，**原样泄漏到构建日志**（一次全量构建约 16500 行噪音）；
2. **所有 .obj 的头文件依赖记录为空**（`ninja -t deps <obj>` 显示 `#deps 0`）——
   **改任何头文件都不会触发重新编译**。

验证过 `chcp 65001` 无效。可行方向：配置时设 `VSLANG=1033` 强制英文诊断（前缀变成 `Note: including file: `）并重新 configure，
或让 configure 与 build 使用同一代码页。

**影响**：本轮所有涉及头文件的改动（Algorithm.h、JSON.cpp 的模板等）都只能靠**全量重建**验证。
在修好之前，增量构建的结果不可信。

---

## B. 健壮性 / 并发（次优先）

### B1. 静态初始化顺序竞争（static init order fiasco）
- [x] **B1** — 已随 F1 一并修复（`55b133e`）。并入 [F1](#f1-类别注册表的跨-tu-静态初始化会静默丢弃注册)，以那条为准

> 本条只写了"顺序未定义"。F1 核实了 `stdc::vlarray` 的默认构造不是常量初始化，
> 补上了具体后果：注册被静默丢弃 + 一处堆泄漏。修的时候看 F1。

[synthrt/lib/Core/SynthUnit.cpp:19](../../synthrt/lib/Core/SynthUnit.cpp#L19) 的 `SynthUnit::Impl::categoryFactories` 是**静态成员变量**，
而 [SingerContrib.cpp:746](../../synthrt/lib/SVS/SingerContrib.cpp#L746)、[InferenceContrib.cpp:456](../../synthrt/lib/SVS/InferenceContrib.cpp#L456) 的
`static ContribCategoryRegistrar<...> registrar;` 在**别的 TU** 里构造时就去写它。跨 TU 初始化顺序未定义。

同仓库里 [`getStaticPluginMap()`（PluginFactory.cpp:17）](../../synthrt/lib/Plugin/PluginFactory.cpp#L17) 用的就是正确的"函数内静态"写法，照它改即可。

---

### B2. `Expected` 的四个缺陷
- [x] **B2a** `explicit operator bool()` 非 const，const 对象无法判真假 — 已修（`e17c7bb`，加 `const`）
- [x] **B2b** `valueOr(const U&) const &` 对 const 左值做 `std::forward<U>`，一旦实例化编译不过 — 已修（`e17c7bb`，两个重载都改成转发引用）
- [x] **B2c** placement new 写成 `&_storage` 而非 `&_storage.err` — 已修（`e17c7bb`，四处统一）
- [x] **B2d** `moveAssign` 非异常安全 — **不修，改为写进文档**

> **B2d 的处置（用户质疑后修正）**：我最初加了 try/catch 兜底，用户问"为什么 moveAssign 要 try catch"。
> 重新检视后我自己也否定了这个补丁——catch 分支只能把对象留在一个 `NoError` 的错误态，
> 这比让异常传播更难排查。
>
> 也试过用 `static_assert(std::is_nothrow_move_constructible_v<T>)` 从类型层面排除风险，
> **失败了**：MSVC 的 `std::map` 移动构造不是 `noexcept`，`Expected<JsonObject>` 直接编译不过。
>
> 最终保留 destroy + reconstruct，在 `moveAssign` 上写 `\note` 说明这一限制。
> `[[nodiscard]]` 同时加到了两个特化上。
> 新增测试：`_ConstContextualConversion`、`_ValueOr`、`_WithContext`、`_MoveAssign`。

---

### B3. 锁的粒度与覆盖不一致
- [ ] **B3a** `packagePathsDirty` 在锁外读 — [SynthUnit.cpp:149](../../synthrt/lib/Core/SynthUnit.cpp#L149)
- [ ] **B3b** `closeAllLoadedPackages()` 遍历 `loadedPackageMap` 不加锁 — [SynthUnit.cpp:436](../../synthrt/lib/Core/SynthUnit.cpp#L436)
- [ ] **B3c** 持全局独占锁期间加载 ONNX 模型（可能数十秒），阻塞所有其它 session 的 open/close — [Session.cpp:664](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L664)
- [ ] **B3d** `ITask::setState()` 完全无同步，但 AcousticInference 从多处并发调用 — [ITask.cpp:28](../../synthrt/lib/Task/ITask.cpp#L28)
- [ ] **B3e** `AcousticInference::start` 先 shared_lock 查 driver 再释放、后面才 unique_lock，TOCTOU — [AcousticInference.cpp:118-124](../../dsinfer/plugins/inferenceinterpreters/acoustic/AcousticInference.cpp#L118-L124)

B3d 最简单：`state` 改 `std::atomic<State>`。
B3c 需要把"占位 + 建 image"拆成两阶段（先在 map 里插入一个 pending 占位并放锁，建完再回填）。

---

### B4. ~~`PhonemeDict::iterator` 直接篡改 sparsepp 内部成员~~ — **降级为备注，不修**
- [x] **B4** — 作者裁定：保持现状

[dsinfer/lib/Support/PhonemeDict.cpp:156-198](../../dsinfer/lib/Support/PhonemeDict.cpp#L156-L198)

`it.row_current = (decltype(it.row_current)) _row;` —— 直接读写 sparsepp 迭代器的内部字段，共 4 处。

**作者裁定（2026-08-01）**：维持现状。sparsepp 的迭代器就是两个指针，直接存这两个指针是最有效率的做法；
改 pimpl 会在查词热路径上平白多一次解引用，得不偿失。

**我原先的评估过重，予以更正**：这些字段是**按成员名**访问的，一旦 sparsepp 改名/删字段，**编译期就会失败**，
不存在我说的"一升级就悄悄坏掉"。真正无保护的只剩"字段名不变但语义变了"这种窄情况，概率低。

若哪天想要零成本的加固，可以加编译期断言（不影响运行时）：
```cpp
static_assert(sizeof(decltype(Impl::map)::const_iterator) == 2 * sizeof(void *),
              "sparsepp iterator layout changed; PhonemeDict::iterator assumes two pointers");
```
但这属于可选项，不是待办。

---

### B5. `createOrtValueFromTensor` 的 `memoryInfo` 参数完全未使用
- [ ] 🔒 **B5** — **保留**（作者已明确：暂不处理）

[dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp:196](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L196)

**事实部分**：调用方构造了 `Ort::MemoryInfo::CreateCpu(...)` 传进来（[:412](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L412)、[:530](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L530)），
函数体从未使用该形参，而是用 `CreateTensor<T>(AllocatorWithDefaultOptions{}, ...)` 新分配再 `memcpy`（[:189-192](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L189-L192)）。
结果是每次推理的每个输入张量都有一次全量拷贝。

同文件中当前无调用者的函数：`createTensorFromOrtValue`（[:227](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L227)）、
`getTensorDataTypeSize`（[:155](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L155)）、
`getTensorDataType`（[:168](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L168)）。

🔒 **保留原因**：作者已决定暂不处理。
另：我此前说"原意是零拷贝、写到一半改掉了"属于推测——也可能是有意为之（拷贝一份可以彻底摆脱输入张量的生命周期约束，
这恰好绕开了 [A9a](#a9-异步推理的两处生命周期问题) 那个 async 悬垂问题的一半）。**在 A9 定案之前不要动这里**，
把参数删掉会丢失将来做零拷贝的接口，改成零拷贝则会加重 A9 的生命周期要求。

---

### B6. `Session::close()` 用 `assert` 保护会导致 UB 的不变量
- [ ] **B6**

[Session.cpp:775](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L775)、[:788](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L788)

Release 构建下 assert 被消掉，紧跟着就是 `images.erase(end_iterator)` / 解引用 end。
改成显式 if + 返回 error（或至少 log critical 后早退）。

---

### B7. 库里直接 `std::abort()`，且三处同构 switch 行为不一致
- [ ] **B7**

- [Contribute.cpp:229](../../synthrt/lib/Core/Contribute.cpp#L229) → `std::abort()`
- [SingerContrib.cpp:739](../../synthrt/lib/SVS/SingerContrib.cpp#L739) → `std::abort()`
- [InferenceContrib.cpp:450](../../synthrt/lib/SVS/InferenceContrib.cpp#L450) → 静默返回成功

同一个 `switch (state)` 模式，三处三种行为。统一成返回 `Error::InvalidArgument`。

---

### B8. 未初始化成员：`SingerImportData::inference`
- [x] **B8a** 补 `= nullptr` — 已修（`3920a22`）
- [ ] 🔒 **B8b** `importList` 持裸指针的脆弱性 — **保留**

[synthrt/lib/SVS/SingerContrib.cpp:26](../../synthrt/lib/SVS/SingerContrib.cpp#L26)

```cpp
InferenceSpec *inference;   // 没有 = nullptr
```
`readSingerImport` 里的栈上 `SingerImportData res` 会把不确定值拷进 `importDataList`；只有走到 `Ready` 状态才被赋值。
中途失败时 `SingerImport::inference()` 返回野指针。

**a)** 修法无歧义：加 `= nullptr`。

**b)** 🔒 **保留**：`importList` 持有指向 `importDataList` 元素的裸指针（[:723](../../synthrt/lib/SVS/SingerContrib.cpp#L723)），
`importDataList` 一旦再被 push_back 触发重分配就全部失效。
**目前时序上是安全的**（`importDataList` 在 `read()` 里一次性构建完，`importList` 在 `Ready` 阶段才建），
所以这不是 bug，只是我认为"很脆"——属于主观判断，不动。真要改需要重新设计 `SingerImport` 的所有权模型，归 D 组。

---

### B9. 失败路径未关闭计时器，输出误导性日志
- [ ] **B9**

[Session.cpp:411-484](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L411-L484)

try 块内多处 `return {}` 都没调 `timer.deactivate()`，会打出 "Finished inference in X seconds"——而实际上失败了。
（块外的两处失败路径反倒记得调。）

---

## E. Error / Expected 的重新设计（本轮新增，非初次审查所列）

> 起因：用户认为 `Error` 与 `Expected` 设计得不好，但 LLVM 的 `Error`（`llvm/Support/Error.h:485`）又过于复杂。
> 讨论后确定两件事：**(a) 开放错误域**、**(b) 因果链**。**先做 (b)**，依据见 E2。

### E1. `Error` 的错误类型是封闭枚举，下游无法扩展
- [x] **E1** — 已修（机制 `a5e09a0`，dsinfer 迁移 `ff71e53` / `b612a00`）

原来的 `Error` 只有一个 `int type`，取值来自 synthrt 自己的 `ErrorType` 枚举。
下游（dsinfer 及第三方插件）想报自己的错误，只能挤进 `SessionError` 这个筐——**它被用了 72 次，全部来自 dsinfer**。

**修法**：改用 `std::error_code` 承载，任何模块注册自己的 `std::error_category` 即可扩展。

**实现中踩到的坑（重要，别拆）**：

1. **`std::is_error_code_enum` 的特化必须排在任何使用之前。** 枚举嵌在 `Error` 类里时，类体内的重载决议
   （`Error(NoError)` 要在 `Error(ErrorCode)` 和 `Error(std::error_code)` 之间选）会**提前实例化**
   `is_error_code_enum_v<ErrorCode>` 并固化为 `false`。症状极其迷惑：类模板查出来是 `true`、变量模板是 `false`，
   MSVC 只报"没有可用的用户定义转换"。现已用 `static_assert` 堵住。
2. **`make_error_code` 必须是 hidden friend，不能是静态成员**——ADL 不查找静态成员函数。
3. 枚举一度被移出类到命名空间作用域（为绕开坑 1），用户指出"这些东西在全局空间很奇怪"。
   核实后确认 std 自己就这么做（`io_errc`、`future_errc`），但**不带作用域的枚举泄漏名字确实是我的错**。
   最终枚举移回类内，靠 hidden friend + 前置 `static_assert` 解决。

**已知的信息损失（用户确认为正常）**：`Error` 转成 `std::error_code` 时，`message` 和 `cause` 都会丢，
因为 `error_code` 只有 (值, category) 两个字段。

**收尾（`ff71e53` / `b612a00`）**：dsinfer 现在有自己的错误域 `ds::ErrorCode`，8 个码——
`NotInitialized`、`AlreadyOpen`、`DriverMismatch`、`DriverLoadFailed`、`InvalidInput`、
`ShapeMismatch`、`SessionFailed`、`ProcessingFailed`。
`srt::Error::SessionError` 这个枚举项**已删除**：它当初存在的唯一理由是下游没地方放错误码，
而 `Error` 改带 `std::error_code` 之后这个前提不成立了。

复核时把"72 处"更正为 **87 处**，且**全部**是 dsinfer 的——没有一处来自 synthrt 自身，
这正是"给 dsinfer 单开一个域"而不是"往 synthrt 枚举里继续加"的依据。
承载文件从 `dsinfer/include/dsinfer/Support/Error.h` 改名为 `ErrorCode.h`，与类型名一致。
测试见 `dsinfer/tests/auto/Support/test_ErrorCode.cpp`（CMake 目标叫 `test_ErrorCode` 而非
`test_Error`，因为后者与 synthrt 侧的目标重名）。

### E2. 错误只有一层 message，下层原因靠拼字符串传递
- [x] **E2** — 已修（机制 `a5e09a0`，落地 `b26339f` / `00ab374`）。**渲染处改 `toString()` 未做**，见条目末尾

**决策依据（用数据定的先后顺序）**：生产代码里**按错误类型分支的地方是 0 处**，
而把下层 `error().message()` 拼进上层字符串的地方有 **41 处**。所以因果链的价值高于开放错误域，先做 (b)。

**调研过的先例**：Java `getCause`、Python PEP 3134 `__cause__`、Go `fmt.Errorf("%w")`、
Rust `std::error::Error::source()` + `anyhow`/`thiserror`、C++ `std::nested_exception`。
**LLVM 的 `Error` 没有链式**——这是本仓库不照抄 LLVM 的一个具体理由。

**实现**：`Error` 加 `std::shared_ptr<const Error> _cause`，配 `cause()` / `withCause()` / `rootCause()`；
`toString()` 渲染整条链，`message()` 仍只给本层。
`Expected` 加 `withContext(code, message) &&`，把当前错误降为 cause 再包一层。

关于内存布局：`anyhow` 同样是堆分配的链表，差别只在它把 vtable 指针和 payload 打包进一次分配。
这里用 `shared_ptr` 是为了让 `Error` 保持可拷贝。

**收尾（`b26339f` / `00ab374`）**：`Error` 最终有**两个**包装方法，用途不同，别混：

| 方法 | 语义 | 对 `code()` 的影响 |
|---|---|---|
| `withCause(Error)` | 包装的错误**有自己的种类** | 换成新的 code |
| `withContext(std::string)` | 只补"失败发生在哪一步" | **保留被包装错误的 code** |

调用方按 `code()` 分支时看不出 `withContext` 的存在，只有 `toString()` 会多渲染一层。
`Expected` 的两个特化都加了对应的 `withContext(std::string) &&` 重载。

**关于"dsinfer 22 处仍在拼字符串"这个说法：已实测更正。** dsinfer 侧的 96 个 `takeError()`
其实**全是无损透传**，并没有把下层 message 拼进上层字符串——原先的计数把透传也算进去了。
因此这一轮只在**调用方知道一个被调方叫不出的名字**的地方补 `withContext`，共 43 处
（manifest 解析路径 + 各推理插件的输入名 / 张量名）。其余透传保持原样，多包一层只是噪音。

⚠️ **渲染处仍调 `message()`**（CLI 的 `log_report_callback`、各插件日志回调），
**链条建好了但默认看不见**。改成 `toString()` 是独立的一件事，未做。

---

## F. 贡献注册与 name / key 扩展机制（2026-08-04 单独审查）

> 起因：用户问"contribute 的注册、name/key 这套扩展机制有没有不正规的地方"。
> 这一节只谈**注册与标识**，不谈 `ContribSpec` 状态机（那是 [D3](#d3-contribspec-状态机的回滚语义)）。

### F1. 类别注册表的跨 TU 静态初始化会静默丢弃注册
- [x] **F1** — 已修（`55b133e`，函数内静态）。与 [B1](#b1-静态初始化顺序竞争static-init-order-fiasco) 是同一件事，以本条为准

[SynthUnit.cpp:19](../../synthrt/lib/Core/SynthUnit.cpp#L19) 的
`stdc::vlarray<ContribCategory *(*)(SynthUnit *)> SynthUnit::Impl::categoryFactories` 是静态成员变量，
写它的两个 registrar 在**别的 TU**：[SingerContrib.cpp:763](../../synthrt/lib/SVS/SingerContrib.cpp#L763)、
[InferenceContrib.cpp:456](../../synthrt/lib/SVS/InferenceContrib.cpp#L456)。跨 TU 动态初始化顺序未定义。

**已核实 `stdc::vlarray` 的默认构造不是常量初始化**（[vlarray.h:426](../../vcpkg/installed/x64-windows/include/stdcorelib/adt/vlarray.h#L426)
调用 `adopt_inline_buffer()`），所以这不是理论风险。registrar 先跑时：

| 步骤 | 状态 |
|---|---|
| 零初始化 | 全零 |
| registrar ×2 跑 | `m_size==m_capacity` 触发扩容 → 堆分配 → `m_begin=堆, m_size=2` |
| `vlarray` 构造函数终于跑 | **NSDMI 把 `m_size` 重设为 0**，`adopt_inline_buffer` 把 `m_begin` 指回内联缓冲 |

**后果：两次注册被静默丢弃，堆上那块泄漏**（此时 `is_inline()` 为真，析构函数不会去释放它）。
`categories` 为空 → 任何声明了 singer / inference 贡献的包加载失败并报"未知类别"。

> **判定更正记录**：我最初写成"容器声称持有 2 个元素、指向未初始化的内联缓冲，随后把垃圾当函数指针调用"。
> **这是错的。** 依据是"`adopt_inline_buffer` 不碰 `m_size`"，但我没去看成员声明——
> [vlarray.h:391-395](../../vcpkg/installed/x64-windows/include/stdcorelib/adt/vlarray.h#L391-L395)
> 五个成员全部带 NSDMI，而 `vlarray_base(const Alloc &)` 的初始化列表只提了 `m_alloc`，
> 所以 `m_size` 必然被重设为 0。**vlarray 的实现没有问题**，两段式的
> "基类构造完、派生类再登记内联缓冲"在正常对象上没有窗口。

**修法**：换成函数内静态，同仓 [PluginFactory.cpp:17](../../synthrt/lib/Plugin/PluginFactory.cpp#L17) 的
`getStaticPluginMap()` 就是正确写法。

---

### F2. `name` 与 `key` 是两个可以各写各的平行标识符
- [x] **F2** — 已修（`e1d0acf`，删 `key()`，`cateKeyMap` 并入 `categories`）

`ContribCategory` 同时持有两个字符串，来源与用途都不同，**没有任何约束要求两者一致**：

| | 来源 | 用途 | 存入 |
|---|---|---|---|
| `name()` | 构造函数参数（[Contribute_p.h:40](../../synthrt/lib/Core/Contribute_p.h#L40)） | 引用文法里 `:singer/main` 的中段 | `categories` |
| `key()` | 纯虚函数返回字面量（[Contribute.h:199](../../synthrt/include/synthrt/Core/Contribute.h#L199)） | manifest 中 `contributes` 的属性名 | `cateKeyMap` |

现有两个类别恰好都写成同一个串（`"singer"` / `"inference"`），纯属巧合。写岔了的表现是
"引用能解析但 manifest 读不到"，或者反过来，且没有任何诊断。

**关键：去复数化之后（[A18](#a18-歌手推理-id-的校验比-spec-宽松得多) 那一轮），manifest 的键已经等于类别名，`key()` 的存在价值归零。**

**修法**：删掉 `key()`，`cateKeyMap` 并入 `categories`。顺带解决 F5。

---

### F3. 同名类别被静默覆盖并泄漏
- [x] **F3** — 已修（`55b133e`，`emplace` 判重，冲突时删除新来的）

[SynthUnit.cpp:29-30](../../synthrt/lib/Core/SynthUnit.cpp#L29-L30)：

```cpp
categories[std::string(category->name())] = category;
cateKeyMap[std::string(category->key())] = category;
```

两个工厂产出同名类别时前一个被覆盖。而析构走
[`stdc::delete_all(categories)`](../../synthrt/lib/Core/SynthUnit.cpp#L38)——按 map 遍历值，
被覆盖的指针再也取不到，**永不析构**。

注册表没有去重、没有反注册、没有枚举。重复注册的典型场景是同一个库既被静态链接又被动态加载。

**修法**：`categories.emplace()` 判返回值，冲突时报错或至少 log critical。

---

### F4. `plugin<T>()` 在空指针上调用成员函数
- [x] **F4** — 已修（`e1d0acf`，三个插件接口加 `static constexpr IID`）

[PluginFactory.h:55](../../synthrt/include/synthrt/Plugin/PluginFactory.h#L55)：

```cpp
return static_cast<T *>(plugin(reinterpret_cast<T *>(0)->T::iid(), key));
```

限定调用不走虚分派、`iid()` 也不解引用 `this`，所以实际能跑——但这是标准意义上的 UB，
UBSan 的 `nonnull-attribute` / `null-pointer-use` 会直接报。

**修法**：改成 `static constexpr const char *IID`，或加一个 traits。前者要求每个插件接口多写一行。

---

### F5. `key` 一词被两个无关概念共用
- [x] **F5** — 已随 F2 一并消失（`e1d0acf`）

- `Plugin::key()`（[Plugin.h:21](../../synthrt/include/synthrt/Plugin/Plugin.h#L21)）：插件在同一 iid 下的自身标识，如 `"onnx"`
- `ContribCategory::key()`：manifest 的 JSON 属性名

同一个命名空间、同一个词、毫无关系。按 F2 删掉后者即可。

小的不一致：`name()` 返回 `const std::string &` 而 `key()` **按值返回** `std::string`——
两个平行概念不同签名，且 `key()` 返回的是常量却每次重新构造。

---

### F6. `ContribCategoryRegistrar` 的带工厂重载不检查 `T`
- [x] **F6** — 已修（`55b133e`，纯删除该重载，未加替代品）

[Contribute.h:236-251](../../synthrt/include/synthrt/Core/Contribute.h#L236-L251)：

```cpp
template <class T>
class ContribCategoryRegistrar {
    static_assert(std::is_base_of<ContribCategory, T>::value, "...");
public:
    inline ContribCategoryRegistrar(ContribCategory *(*fac)(SynthUnit *));  // 完全不看 T
    inline ContribCategoryRegistrar();                                       // 用 T
};
```

带工厂的重载里 `T` 不参与任何检查，顶上的 `static_assert` 形同虚设——传一个返回别的类别的工厂照样编过。

**修法**：把工厂签名改成 `T *(*)(SynthUnit *)`。

---

### F7. 公开 API 在宣传一个不存在的扩展点
- [x] **F7** — 已解答并修正（`e1d0acf` 之后的一轮）

**作者裁定：插件不能注册 contribute category。类别集合是 synthrt 自己的、封闭的。**

裁定之前查到的两条事实，都指向同一个结论：

1. **插件在结构上不可能注册类别。** 插件通过 `PluginFactory::plugin(iid, key)` 懒加载，而
   `SynthUnit` **就是** `PluginFactory`——加载插件的前提是 SynthUnit 已存在，而 SynthUnit
   在构造时就把类别列表消费完了。插件的注册永远到不了加载它的那个 unit。
   （静态链接的插件除外，它们在 `main` 之前注册，赶得上。）
2. **根本没有"贡献类别"的插件接口。** 三个接口 `InferenceDriverPlugin`、
   `InferenceInterpreterPlugin`、`SingerProviderPlugin` **全是既有类别内部的消费者**。

于是真正的缺陷不是机制，而是**公开 API 长得像扩展点**：`ContribCategoryRegistrar<T>` 摆在
公开头文件里，`SynthUnit::registerCategoryFactory` 是它的入口。

**已做的修正：**

- `SynthUnit::registerCategoryFactory` 与 `Impl::categoryFactories()` **删除**，公开 API 不再有注册入口
- 注册改用 `stdc::StaticRegistry<ContribCategoryFactoryBase>`——常量初始化的侵入式链表，
  零动态初始化，顺带把 F1 的成因从根上去掉
- `ContribCategoryFactoryBase` / `ContribCategoryFactory<T>` 留在公开头文件：
  公开头文件里的友元声明要引用它们，藏起来会造成"引用了看不见定义的类型"
- 注册表别名也公开，但**改用导出版实例化宏 + `extern template` 声明**，
  否则库外拿到的是一个空列表（见下）
- 文档写明：读它是查询本构建支持哪些类别的方式，从外部注册不会生效

**剩下的设计事实（不改，但要知道）：** 每个 SynthUnit 必然拥有全部已注册类别，
无法构造"只支持 singer"的运行时。当前只有 2 个类别，代价可忽略。

> **判定失误记录**：我最初把 `ContribCategoryRegistrar` 摆在公开头文件这件事，
> 当成了"实现刻意支持插件扩展"的证据，并据此给 dspk 规范写了论据
> （变更 6 的"允许第三方 category"）。**那条论据是错的，已撤回**，见
> [dspk-spec-changes.md](dspk-spec-changes.md) 第 6 节开头的更正。
> 教训：公开 API 的**形状**不等于设计意图，断言"插件可以 X"之前要验证 X 真的走得通。

> **`Contribute.cpp` 那条注释**（我写的）说 "Categories are registered by plugins"，与事实不符。
> 已在 `eb25a18` 改掉。

---

### F8. 导出一个 `StaticRegistry` 需要两件事，缺一是静默的错误答案
- [x] **F8** — 已处理

把 `ContribCategoryRegistry` 放进公开头文件后，光把实例化宏换成 `_EXPORT` 版**不够**：

```
error LNK2001: 无法解析的外部符号 StaticRegistry<ContribCategoryFactoryBase>::_head
```

MSVC 下游只看到类模板声明，会隐式实例化**自己的一份** `_head`，而 `_head`/`_tail` 的定义模板
只存在于 `Contribute.cpp`。必须再加一条显式实例化声明：

```cpp
#ifndef SYNTHRT_LIBRARY
extern template class SYNTHRT_EXPORT stdc::StaticRegistry<srt::ContribCategoryFactoryBase>;
#endif
```

`#ifndef` 是必需的：库内 `SYNTHRT_EXPORT` 是 `dllexport`，而实例化**声明**带 dllexport 是错的
（clang 报 `-Wdllexport-explicit-instantiation-decl`），库内也不需要——定义就在 `Contribute.cpp`。

⚠️ **失效模式是静默的**：非导出时是链接错误（还算响亮），但若 `extern template` 那行被删掉，
下游拿到的是一个**空列表**，看起来像"没有任何类别注册过"。
已加回归测试 `test_SynthUnit_RegistryIsReachableFromOutside`——测试二进制是独立模块，正好能发现。

---

## C. 代码质量 / 可维护性（可批量处理）

- [ ] **C1** `goto` + 标签的错误处理遍布核心逻辑，是 A1 那类 bug 的温床
  [SynthUnit.cpp](../../synthrt/lib/Core/SynthUnit.cpp)（4 处 `do{...}while(false)` + `goto out_xxx`）、
  [PackageRef.cpp:233-304](../../synthrt/lib/Core/PackageRef.cpp#L233-L304)、
  [Session.cpp:673-755](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L673-L755)、
  [PhonemeDict.cpp:86-152](../../dsinfer/lib/Support/PhonemeDict.cpp#L86-L152)

- [ ] **C2** 重复代码
  - `isValidPackageIdentifier` 同一正则复制 3 份：[SynthUnit.cpp:21](../../synthrt/lib/Core/SynthUnit.cpp#L21)、[PackageRef.cpp:25](../../synthrt/lib/Core/PackageRef.cpp#L25)、[SingerContrib.cpp:60](../../synthrt/lib/SVS/SingerContrib.cpp#L60)
  - `readJsonObjectFile` 在 [SingerContrib.cpp:65](../../synthrt/lib/SVS/SingerContrib.cpp#L65) 与 [InferenceContrib.cpp:43](../../synthrt/lib/SVS/InferenceContrib.cpp#L43) 逐字重复
  - `sessionRun` / `sessionRunAsync` 约 60 行几乎完全相同的输入准备代码（[Session.cpp:403-443](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L403-L443) vs [:521-561](../../dsinfer/plugins/inferencedrivers/onnxdriver/internal/Session.cpp#L521-L561)）


- [ ] **C4** 大量 signed/unsigned 比较（`-Wsign-compare` 会刷屏）
  `i < contributes.size()`（int vs size_t）、`resampled.size() != targetLength`、
  `embedding.size() != hiddenSize`、`file.gcount() != byteSize`、`dataLength != dataLengthFromShape` 等。
  建议开 `-Wall -Wextra` 后集中清一轮。

- [ ] **C5** 命名
  `error1` / `error2` / `spec1` / `it2` / `it3` 之类的机械编号，在一个注释和文档都相当讲究的仓库里很突兀。

- [x] **C6** `loadSpeakerEmbedding` 里 `if (!file)` 之后的 `gcount()` 分支是死代码（短读已置 failbit） — 已修（`3920a22`）
  [dsinfer/util/inferutil/src/SpeakerEmbedding.cpp:29-37](../../dsinfer/util/inferutil/src/SpeakerEmbedding.cpp#L29-L37)

- [ ] **C7** `PhonemeDict` 的错误码来源不一致：[:53](../../dsinfer/lib/Support/PhonemeDict.cpp#L53) 用 `make_last_error()`（Windows 下是 utf8 category），[:68](../../dsinfer/lib/Support/PhonemeDict.cpp#L68) 直接 `std::system_category()`。而且 ifstream 失败并不保证设置 `errno`。

- [x] **C8** `OnnxDriver::Impl::hLibrary` 是从未使用的死成员 — [OnnxDriver.cpp:144](../../dsinfer/plugins/inferencedrivers/onnxdriver/OnnxDriver.cpp#L144)（行号已随 A10c 的改动前移） — 已修（`3920a22`）

- [x] **C9** ~~`LogCategory` 的过滤规则是空实现~~ — **已消失（`a55dc98`）**
  `synthrt` 不再自己实现日志。[Logging.h](../../synthrt/include/synthrt/Support/Logging.h) 从 209 行缩到 61 行，
  只把 `stdc::LogContext` / `Logger` / `LogCategory` 别名过来，并把 16 个 `srtXxx` 宏转发到 `stdcXxx`；
  `synthrt/lib/Support/Logging.cpp`（145 行）整个删除。
  过滤规则现在是 stdcorelib 的责任，本仓库不再有这个 TODO。

- [ ] 🔒 **C9b** README 用法示例 — **保留**（可能是我看错了）
  [README.md:83-85](../../README.md#L83-L85) 写 `find_package(dsinfer)` + `dsinfer::dsinfer`，而注释说 `CMAKE_PREFIX_PATH` 是"`synthrt` install directory"。
  我最初判断为"不符"，但**这个判断可能是错的**：本仓库的 synthrt 和 dsinfer 装在同一个 prefix 下，
  所以指向该 prefix 再 `find_package(dsinfer)` 是能工作的。留着待确认，别当 bug 改。

- [ ] **C10** 文档与代码的版本对应关系缺失
  manifest 只认 `"$version": "1.0"`（[InferenceContrib.cpp:141](../../synthrt/lib/SVS/InferenceContrib.cpp#L141)、[SingerContrib.cpp:242](../../synthrt/lib/SVS/SingerContrib.cpp#L242)），
  而 [docs/ds-spec-2.3.md](../../docs/ds-spec-2.3.md) 标的是 2.3，[docs/dsinfer-level-1-draft.md](../../docs/dsinfer-level-1-draft.md) 是"草案"，两者与代码的对应关系没有任何说明。

- [x] ~~**C3** 标识符校验规则冲突~~ — **已由 spec 裁决，升级为 [A18](#a18-歌手推理-id-的校验比-spec-宽松得多)**

- [ ] **C12** 六个插件声明 `FEATURES cxx_std_17`，实际编译为 C++20
  `onnxdriver` 与五个推理解释器（`acoustic` / `duration` / `pitch` / `variance` / `vocoder`）共 22 个 TU
  编译时带的是 `-std:c++20`，而 `synthrt`、`dsinfer`、`inferutil` 等 44 个 TU 是 `-std:c++17`。
  这六个目标恰好就是链接 `stduuid` 的六个。

  **成因**：vcpkg 的 stduuid port 默认不启用 `gsl-span` 特性，于是上游按 `UUID_USING_CXX20_SPAN=ON`
  配置，导出的目标带 `INTERFACE_COMPILE_FEATURES "cxx_std_20"`。CMake 解析 `CXX_STANDARD` 时取
  「本目标的 `FEATURES`」与「所有链接项的接口 feature」的**最大值**，`LINKS_PRIVATE` 不改变这一点——
  PRIVATE 只阻止要求继续传给**下游**，这也正是 `dsinfer` / `inferutil` 仍是 C++17 的原因。

  装出来的 `uuid.h` 本身是自适应的（`__cplusplus >= 202002L` 用 `<span>`，否则 `<gsl/span>`），
  硬要求只存在于 CMake 目标上。但**不能靠在消费侧清掉 `INTERFACE_COMPILE_FEATURES` 绕开**：
  那个配置下 Microsoft.GSL 根本没被安装，降到 C++17 会 include 一个不存在的头。

  *两条出路*：在 `vcpkg.json` 里给 stduuid 请求 `gsl-span` 特性（引入 Microsoft.GSL 依赖，换来全仓 C++17），
  或把这六个插件的 `FEATURES` 如实写成 `cxx_std_20`，让标准是**声明**的而不是**继承**来的。**未定，未动。**

- [ ] 🔒 **C11** 可能的规格偏差 — **保留**（纯推测，非 bug）
  [InputParserCommon.cpp:73-86](../../dsinfer/util/inputparser/src/InputParserCommon.cpp#L73-L86) 的 `parseLibrosaPitch` **强制要求** cents 部分（`+0` / `-25`），
  纯音名 `"C4"` 会被判非法。librosa 的 `note_to_midi` 是接受 `"C4"` 的。若属有意收紧，建议在函数注释里写明。
  另：升降号 `#` / `b` 的匹配是大小写敏感的，而音名是大小写不敏感的（[:44](../../dsinfer/util/inputparser/src/InputParserCommon.cpp#L44) vs [:57](../../dsinfer/util/inputparser/src/InputParserCommon.cpp#L57)）。

---

## D. 设计层面（**先记，最后讨论后再动**）

> 这一节的每一条都会改变对外 API 或整体结构，**不要顺手改**。等 A/B/C 收敛后单独开一轮讨论，确定方案再动手。

- [ ] **D1** `SynthUnit::open()` 的双错误通道
  [SynthUnit.cpp:143 起](../../synthrt/lib/Core/SynthUnit.cpp#L143)。
  重复包 / 循环依赖 / 依赖缺失 / contrib 初始化失败——**全部返回成功的 `Expected<PackageRef>`**，错误藏在 `PackageRef::error()` 里，只有 `isLoaded()` 为 false；
  而路径非法返回的是真正的 error。同一个 API 两套错误语义，调用方极易漏判。
  *待讨论*：统一成 `Expected`（失败即 error），还是统一成"永远返回 PackageRef，用 error() 查"，抑或拆成 `open()` / `tryOpen()` 两个函数。

- [ ] **D2** `JsonValue` 的 proxy 容器方案是否值得继续
  [JSON.cpp:29-455](../../synthrt/lib/Support/JSON.cpp#L29-L455)。用 proxy_map/proxy_vector 替换 nlohmann 的底层容器，
  只为让 `JsonObject = std::map<std::string, JsonValue>` 能对外暴露且 ABI 稳定。代价是 A5/A6/A7 三个 bug 都出在这里，
  且 proxy 容器要跟着 nlohmann 的内部要求走（`insert_return_type`、迭代器分类等）。
  *待讨论*：继续修补 vs 换成"不透明句柄 + 显式访问器"（放弃 `std::map` 兼容接口）vs 直接暴露 nlohmann 类型并放弃 ABI 稳定承诺。

- [ ] **D3** `ContribSpec` 状态机的回滚语义
  `Initialized → Ready → Finished → Deleted` 四态 + 三段失败回滚（[SynthUnit.cpp:252-340](../../synthrt/lib/Core/SynthUnit.cpp#L252-L340)）
  逻辑正确但极难读，A1 就是它的产物。
  *待讨论*：抽成一个显式的 `LoadTransaction` / scope guard，让回滚由析构负责。

- [x] **D7** `ContribLocator` 的字符串语法与官方引用语法不一致 — 已解决（`4d222bc`），**并反过来推动规范修改**

  > **结论与"保留"时的预期相反**：不是"另写一个解析器"或"同时支持两套语法"，而是**设计了第三套文法，
  > 旧文法直接删除**（用户裁定：内测期，不留历史包袱）。完整推导见
  > [contrib-reference-grammar.md](contrib-reference-grammar.md)，§9 是实现记录。
  >
  > 促成变更的关键论点（用户提出）：官方语法**给 singer 和 inference 用了不同的格式**
  > （`[...]` vs `:...`），那么每加一种 contribute 类型就得加一种格式，无法扩展。
  > 需要的是**统一的定位符**。
  >
  > 最终文法：
  > ```ebnf
  > reference     = package-part [ ":" contrib-part ] / ":" contrib-part
  > package-part  = package-id [ "=" version ]
  > contrib-part  = [ category "/" ] contrib-id
  > ```
  > `=` 取代 `@`（灵感来自 vcpkg 的 `port=version`），版本可选；category 可选，因为同一包内
  > singer 与 inference 重名的可能性本就很低。
  >
  > 附带解决：A2b、A3、A17、A18、C3。
  > 附带发现：`stdc::VersionNumber::fromString` 对尾部垃圾很宽容，**不能用它判断版本是否合法**，
  > 已另写 file-local 的 `isValidVersion()`。这个宽容已经坑过两次（此处 + A2a）。
  >
  > 六条待向 dspk org 提 PR 的规范变更记在 [dspk-spec-changes.md](dspk-spec-changes.md)。
  > 其中**第 6 条（`contributes` 子键改可选）只记录、代码未改**——当前实现符合现行规范，改了就是主动偏离。

  <details><summary>原始记录</summary>

  [package-specification 1.0](https://dspk.diffscope.org/docs/1.0/package-specification.html) 定义的文本引用格式是：

  | 类型 | 官方语法 | 例子 |
  |---|---|---|
  | 包 | `package-id@version` | `vendor/sample@1.0.0.0` |
  | 推理 | `package-id@version:inference-id` | `vendor/sample@1.0.0.0:acoustic` |
  | 歌手 | `package-id@version[singer-id]` | `vendor/sample@1.0.0.0[main]` |

  而 `ContribLocator` 用的是 `package[version]/id`——分隔符（`@`/`:` vs `[]`/`/`）和 `[]` 的含义（版本 vs 歌手 id）都不同。

  **目前不构成规范违背**：歌手清单的 imports 用的是独立 JSON 字段（`id`/`version`/`inferenceId`）而非拼接字符串，
  `ContribLocator::fromString` 生产代码里只被 [PackageListConfig](../../dsinfer/lib/Support/PackageListConfig.cpp#L78) 用于**它自己的**配置文件格式（非 DSPK 产物）。

  *待讨论*：若将来要解析 spec 风格的引用串（比如给 CLI 或包管理器用），需要另写一个解析器，或让 `ContribLocator` 同时支持两套语法。
  现在先记着，避免有人误以为 `ContribLocator` 就是 spec 里那个引用格式。

  </details>

- [ ] 🔒 **D4** 每实例 vs 全局的 ONNX Runtime 初始化 — **保留**（整节 D 均为待讨论）
  [OnnxDriver.cpp:160-165](../../dsinfer/plugins/inferencedrivers/onnxdriver/OnnxDriver.cpp#L160-L165) 的 `impl.loaded` 是**每个 Impl 实例**的标志，
  错误信息却写着 "initialized by another instance"。而 `Ort::InitApi` 是**进程全局**的。
  创建两个 OnnxDriver 实例会重复 dlopen + 重复 InitApi。
  *待讨论*：把 ORT 加载状态提升为进程级单例，还是明确"每进程只允许一个 driver 实例"并加检查。

- [ ] **D5** `NO<T>`（`shared_ptr` 子类）作为公共 API 载体 — **部分处理（`1938962`），所有权部分已定案，类型识别部分仍待讨论**

  [NamedObject.h](../../synthrt/include/synthrt/Core/NamedObject.h)。继承 `std::shared_ptr`，
  `as<U>()` 用的是 `static_pointer_cast`（无类型检查），配合 `objectName()` 字符串比较来做运行时类型识别。

  **已做（`1938962`）**：
  - **`NO` 瘦身**：原来手写了 12 个构造函数转发（其中一个是死代码），全部换成 `using Base::Base`，
    从约 60 行缩到 15 行。用户要求："只要能继承 shared_ptr 所有的构造函数就行"。
  - **新增 `UNO`**（`unique_ptr` 子类），用于单一所有者的场合。
    `UNO` 移动即可转成 `NO`，所以工厂可以交出独占所有权，而不必替调用方决定要不要共享。
    `UNO::as<U>()` 返回**裸指针**（所有权留在原处），这点与 `NO::as<U>()` 不同。

  **调查结论（用数据定的）**：35 处 `NO` 的所有权用法中，约 **22 处其实是独占的**，8 处是"注册表持有 + 借用者取用"，
  只有约 5 处（张量跨推理阶段传递）是真正需要共享的。
  **是"按值返回 `NO` 的访问器"把本可独占的成员变成了共享的**——这是问题的根源。

  已转换：`InferenceSession` 相关 13 处。剩余转换见待办表（按爆炸半径排序）。

  ⚠️ **张量那一项做过又撤销了（2026-08-04）。** 我曾把 7 个 `Tensor::create*` 工厂改成返回
  `UNO<Tensor>`，编译只错一处——其余 27 个调用点靠 `unique_ptr → shared_ptr` 的隐式转换照样通过，
  **所以"改完能编译"在这里完全不构成证据**。作者指出 **`ITensor` 本来就该是共享类型**：
  它存进 `map<std::string, NO<ITensor>>`、跨推理阶段传递，`AcousticInference` 里甚至有
  `// ref count +1` 的注释。实测也印证：27 个调用点几乎全是 `inputs["x"] = exp.take()`，
  `UNO` 的存活窗口只有一条语句。**已全部还原。**

  > **教训**：判断该不该 `UNO`，看的是**类型本身的生命周期**，不是某个持有者内部是否独占。
  > `TensorHelper` 内部确实独占它的 `_tensor`，但那东西一旦交出去就是共享的。

  ⚠️ **`as<U>()` 的风险描述更正**：我曾说 `static_pointer_cast` 跨兄弟基类转换是 UB。
  **实测是编译错误**（MSVC `error C2440`）——`static_pointer_cast` 背后是 `static_cast`，
  兄弟基类之间根本没有转换路径。真正的 UB 是**向下转换到对象实际不是的那个类型**，
  那才是 `as<U>()` 无类型检查带来的风险。

  *仍待讨论*：`as<U>()` 是否该换成 `dynamic_pointer_cast` 或加一个轻量类型 tag。**这部分没动。**

- [ ] **D6** 测试覆盖
  [synthrt/tests/auto/](../../synthrt/tests/auto/) 只覆盖 Contribute / PackageRef / DisplayText / Expected / JSON / Logging；
  dsinfer 侧只有 PhonemeDict 和一个 onnxdriver 冒烟测试。
  **上面 A 组的问题基本没有一条能被现有测试捕获。**
  *待讨论*：至少要为 `ContribLocator` round-trip、`JsonObject` 迭代器/erase、`PhonemeDict::load` 失败路径、
  `SynthUnit` 依赖解析（循环/缺失/重复）补上回归测试，作为 A 组修复的验收条件。

---

---

## 🔒 保留清单（速查）

以下条目**只记录，不动手**。动它们之前必须先和作者确认意图。

| 条目 | 位置 | 保留原因 |
|---|---|---|
| ~~A2b~~ | ~~`ContribLocator::fromString`~~ | **已解锁并修复**（`4d222bc`，随新文法一并解决） |
| ~~A3~~ | ~~`findContributes`~~ | **已解锁并修复**（`4d222bc`，注释重写） |
| **A5** | `JsonValue::Undefined` | 作者明确暂不处理；与 D2 绑定 |
| **A11b** | `satisfyMouthOpening` | 只写不读已确认，但属于"必需/可选/有意不校验"哪一类未知；文档里没有该参数 |
| **B5** | `createOrtValueFromTensor` | 作者明确暂不处理；且"未使用参数"可能是有意（规避 A9 的生命周期约束） |
| **B8b** | `SingerImport` 裸指针 | 当前时序安全，"脆弱"是主观判断 |
| ~~B4~~ | ~~sparsepp 迭代器~~ | **已裁定不修**，且原评估过重（改名会编译期报错，非静默失效） |
| **C9b** | README 用法示例 | 我的"不符"判断本身可能有误 |
| **C11** | `parseLibrosaPitch` | 纯推测的规格偏差，非 bug |
| **规范变更 6** | `contributes` 子键改可选 | 已记入 [dspk-spec-changes.md](dspk-spec-changes.md)，**代码故意未改**——当前实现符合现行规范 |
| **D1–D4、D6** | 设计层 | 待讨论（D5 部分处理，D7 已解决） |

---

## 进度

| 分组 | 可动手 | 🔒 保留 | 已完成 |
|---|---|---|---|
| A 确定 bug | 24 | 2 | **23**（仅剩 A16 环境问题） |
| B 健壮性 | 12（−B4） | 2 | **7**（B1 B2a–d B8a + B4 裁定不修） |
| C 代码质量 | 10（−C3，升为 A18；+C12） | 2 | **3**（C6 C8 C9） |
| E Error 重设计 | 2 | 0 | **2** |
| F 注册与标识 | 8 | 0 | **8**（F1–F8） |
| D 设计 | 1（D5 剩类型识别部分） | 5 | **1**（D7） |
| **合计** | **57** | **11** | **44** |

### 剩余可动手的条目
| 条目 | 性质 | 风险 |
|---|---|---|
| **B6 B7 B9** | assert / abort / 计时器 | 低 |
| **B3a–e** | 锁粒度 | B3d 最简单（`state` 改 atomic）；B3c 要拆两阶段 |
| **C1 C2 C4 C5 C7 C10** | 代码质量 | 低，可批量 |
| **C12** | 六个插件的 C++ 标准名不副实 | 低，但要先定走哪条路 |
| **E2 尾巴** | 渲染处（CLI + 各插件日志回调）改用 `toString()`，否则因果链看不见 | 低 |
| **D5 收尾** | NO→UNO 剩余 16 处 | 见下表 |
| **A16** | 构建环境，非代码 | 需用 `VSLANG=1033` 重新 configure |

### NO/UNO 剩余转换（按爆炸半径）
| 目标 | 处数 | 阻碍 |
|---|---|---|
| ~~`TensorHelper::_tensor`~~ | ~~1~~ | **已撤销**——`ITensor` 本来就是共享类型，见 D5 |
| schema / configuration / options | 4 | 访问器现在**按值返回 `NO`**，要先改成返回裸指针 |
| `XxxResult result` | 5 | 需改 `ITask` API——`start()` 既返回结果又存一份，共享是 API 形状造成的 |
| `driver` / `interp` / `prov` | 7 | pool / cache 持有，借用者应改裸指针；需改 `getInferenceDriver()` 签名 |

### 已裁定不修
- **B4** sparsepp 迭代器内部字段 — 作者裁定保持现状，我的原评估过重（详见该条）
- **B2d** `moveAssign` 异常安全 — 两种修法都比现状差，改为文档说明（详见该条）

### 验证方式
- 全量重建（`ninja -t clean` + build）：**exit 0**
- **`ctest` 10 个用例全过**（每个类一个可执行文件，见 `1d969c5`）
- 手动套件 `dsinfer/tests/manual/` **5/5**，含真实 ONNX 推理。不进 ctest：`onnxdriver` 要模型，`txtdict` 要词典路径
- ⚠️ 由于 A16，增量构建不可信；每轮改动后必须全量重建。
- 断言总数约 **475**（Support 384 + Core 84 + PhonemeDict）。
  `ContribLocator` 有 `_Parse` / `_Reject` / `_RoundTrip` / `_VersionIsNormalized` / `_Segments`；
  **A 组早期修复（A1、A6–A15）仍无测试覆盖**，D6 依旧成立。

> **验证的教训（2026-08-04）**：改完 F1 后 8/8 全过，但当时**没有任何自动测试构造过 `SynthUnit`**，
> 注册链路整个断掉也不会变红。"全过"必须先确认测试确实覆盖了改动路径，
> 用 `--report_level=short` 看断言数是最快的核对方式（空 suite 也会"通过"）。

> **另一个坑（2026-08-04）**：链接刚生成的 exe/dll 时偶发
> `The process cannot access the file because it is being used by another process`，**无编译错误**，重跑即好。
> ⚠️ **它会让 ninja 以非零码退出，而此时 ctest 跑的是上一轮的旧二进制，给出虚假的"全过"。**
> 已撞上一次。**看到测试通过之前必须先确认 `BUILD=0`。**

> **批量改动要核对计数，不能只看编译（2026-08-04）**：给 dsinfer 补 `withContext` 时，
> sed helper 丢了行号参数 → 表达式没有地址 → 全局替换，且后续表达式反复匹配已改过的文本、层层嵌套。
> `PitchInference` 被塞进 158 个 `withContext`（应为 15），`VarianceInference` 42 个（应为 10）。
> **而且编译完全通过**——`withContext` 返回 `Error`，`Error` 又有 `withContext`，语法合法。
> 靠 `grep -o <pattern> | wc -l` 数实际次数才发现（`grep -c` 数的是行数，会漏）。
