# 贡献类别的三层改造方案

> 把 `Impl` 现在兼着的两个身份拆开：**私有实现**归 `Impl`，**扩展接口**独立成 `Definition`。
>
> 依据 `3c962f6` + 工作区里「id 挪进 desc.json」那笔改动。

---

## 一、要解决的是什么

`Contribute_p.h` 的文件头写着：

> nothing here is a stable interface, and all of it may change between any two versions

而它**随库安装**，并且是造一个新贡献类别的**必经之路**——wolf 的 `LanguageCategory` 就是靠它写出来的。

一个「必须公开、扩展必需」的头文件同时声明自己不稳定，这不是文档没写好，是**一个类担了两个身份**：

| `ContribSpec::Impl` 的成员 | 身份 |
|---|---|
| `virtual read()` | 扩展接口——造类别的人覆盖它 |
| `fmtVersion` | 扩展接口——造类别的人写它 |
| `category` `id` `state` `package` | 私有实现——框架自己的账 |

`ContribCategory::Impl` 更极端：`contributes` / `indexes` / `su_mtx()` / `findContributes` **没有一样是扩展者需要的**。wolf 和 inference 派生它，纯粹是为了塞自己的缓存（`interpreters` / `providers`），不是为了用基类成员。

---

## 二、验收标准

1. **`Contribute_p.h` 搬回 `lib/Core/`，不再随库安装**，而它那句「不是稳定接口」变成实话。
2. wolf 只包含公开头文件即可造出 `LanguageCategory`，**不碰任何 `_p.h`**。
3. 一个不需要专属消费者 API 的类别（如测试里的 `SampleCategory`）**只写两个类**，不是四个。

---

## 三、命名

`Definition`。它描述的是「这一类 contribute 是什么、怎么读」。

- `ContribSpecDefinition` / `ContribCategoryDefinition`（基类，公开）
- `LanguageSpec::Definition` / `LanguageCategory::Definition`（作者写的）

> 不用 `Backend`：这个项目里 backend 那根轴已经是 `InferenceDriver`（onnx / torch），
> 拿它命名一个和执行无关的东西会分不清。
> 不用 `Schema`：`InferenceSchema` 已占，同词两义正是 F5 的形状。

---

## 四、分界线

按「**造类别的人需不需要看见**」切，不按「现在写在哪」切。

### ContribSpec

| 成员 | 去处 |
|---|---|
| `virtual read(basePath, entry)` | **Definition** |
| `fmtVersion` | **Definition** |
| 作者自己的状态（`className` `apiLevel` `schema`…） | **Definition**（直接放成员，不必再套一层 pimpl） |
| `category` `id` `state` `package` | Impl（私有；`id` `state` `category` 已有公开访问器） |

### ContribCategory

| 成员 | 去处 |
|---|---|
| `virtual parseSpec()` `virtual loadSpec()` | **Definition** |
| 作者自己的缓存（`interpreters` `providers`…） | **Definition** |
| `name` `su` | Impl（`name()` / `SU()` 已是公开访问器） |
| `contributes` `indexes` `su_mtx()` `findContributes()` | Impl（私有） |
| `find(locator)` | 保留在 `ContribCategory` 的 protected 区，Definition 经反向指针调用 |

---

## 五、头文件布局

```
Core/Contribute.h            消费者   ContribLocator · ContribSpec · ContribCategory
Core/ContribDefinition.h     扩展者   ContribSpecDefinition · ContribCategoryDefinition
                                      ContribCategoryFactory · ContribCategoryRegistry
lib/Core/Contribute_p.h      私有     ContribSpec::Impl · ContribCategory::Impl
```

`ContribCategoryFactory` 与 `ContribCategoryRegistry` 一并挪走——**纯调库的人从来不碰它们**，它们现在挤在 `Contribute.h` 里是同一个毛病的另一面。

---

## 六、造一个类别，改造前后

```cpp
// 现在：四个类，其中两个必须派生 _p.h 里的 Impl
class LanguageSpec : public ContribSpec {
    class Impl : public ContribSpec::Impl { ... };        // ← 只为了覆盖 read()
};
class LanguageCategory : public ContribCategory {
    class Impl : public ContribCategory::Impl { ... };    // ← 只为了塞自己的缓存
};

// 改造后：仍是四个类，但两个 Definition 都在公开头文件里
class LanguageSpec : public ContribSpec {
    class Definition : public ContribSpecDefinition { ... };
};
class LanguageCategory : public ContribCategory {
    class Definition : public ContribCategoryDefinition { ... };
};
```

> **类的个数没变，这一点要说清楚。** 收益不在少写代码，在三处：
> `_p.h` 名副其实地私有了；扩展的 vtable 和消费者类的 vtable 各自演化，以后加一个扩展虚函数
> 不再是对所有调库者的 ABI 破坏；消费者头文件不再背着扩展机械。
>
> 唯一真的变少的情况：**不需要专属消费者 API 的类别只写两个 Definition**，
> 连 `LanguageSpec` / `LanguageCategory` 都不用有。测试里的 `SampleCategory` 正是这种。

---

## 七、顺带定下 entry 的边界

`desc.json` 里一条 contribute 的形状，通用层只管两件事：

```jsonc
{ "id": "pitch", "path": "./inferences/pitch/inference.json" }
//  ^^ 通用      ^^^^ 类别自己的事
```

- **`id` 归通用层**：它是 `ContribLocator` 的解析目标，而那套引用语法是跨类别通用的。通用层校验它是 segment、在本类别内不重复，然后写到 spec 上。
- **其余字段归类别**：`parseSpec` 收到**整个 entry 对象**和 `basePath`，`path` 这个键名不再由通用层规定。类别想叫 `program`、想拆成三个字段、想完全内联不指向文件，都行。

> VSCode 的先例支持这一条：`grammars` 用 `path`、`languages` 用 `configuration`、
> `jsonValidation` 用 `url`、`debuggers` 用 `program`，`commands` 根本不指向文件。
> 它之所以能这么自由，是因为**它的 contribution point 集合是封闭的**（扩展加不了新的）。
> 我们的类别是开放的，所以才需要 `id` 这一格是通用的——**VSCode 没有跨类别通用的引用语法，我们有**。

`parseSpec` 因此变成：

```cpp
virtual Expected<ContribSpec *> parseSpec(const std::filesystem::path &basePath,
                                          const JsonObject &entry) const = 0;
```

比现在还少一层转换：通用层不再替类别解析路径。

---

## 八、分四批

```mermaid
graph LR
    B1["<b>批 1</b><br/>ContribSpecDefinition<br/>read + fmtVersion 挪出"] --> B2["<b>批 2</b><br/>ContribCategoryDefinition<br/>parseSpec + loadSpec 挪出"]
    B2 --> B3["<b>批 3</b><br/>entry 交给类别<br/>parseSpec 收 JsonObject"]
    B3 --> B4["<b>批 4</b><br/>_p.h 搬回 lib/<br/>+ wolf 验证"]
    style B4 fill:#238636,color:#fff
```

### 批 1 — `ContribSpecDefinition`

新建 `Core/ContribDefinition.h`，`ContribSpec` 的 protected 构造函数改收 `std::unique_ptr<Definition>`。
`InferenceSpec` / `SingerSpec` 的 `Impl` 改名并改基类。

**验收**：ctest 全过；`InferenceSpec::Impl` 这个名字消失。

> **要定的一件事**：作者访问自己状态时多一跳（`impl.name` → `def().name`）。
> 一次性改访问器即可，但要确认你接受这个写法。

### 批 2 — `ContribCategoryDefinition`

`parseSpec` / `loadSpec` 从 `ContribCategory` 挪到 Definition；Definition 持有反向指针以调用 `find()`、`SU()`。
`ContribCategoryFactory` / `ContribCategoryRegistry` 同时挪进新头文件。

**验收**：ctest 全过；`SampleCategory` 改写成「只有两个 Definition」的形态，作为验收标准 3 的证据。

### 批 3 — entry 交给类别

`parseSpec` 改收 `const JsonObject &entry`；通用层只留 `id` 的校验与去重。
三个类别各自从 entry 里取自己要的键（现在都是 `path`）。

**验收**：`test_Contribute` 里那组「entry 写错」的用例仍然全过；新增一个用自定义键名的类别用例，证明 `path` 不再是硬性约定。

### 批 4 — `_p.h` 回私有 + wolf 验证

`Contribute_p.h` 从 `include/` 搬到 `lib/Core/`，从安装列表移除；`NamedObject_p.h` / `PluginFactory_p.h` 一并复查是否还需要公开。

**验收**：**wolf 删掉所有 `_p.h` 的 include 仍能构建，2/2 通过。** 这是整件事的终点。

---

## 九、已知取舍

**类的个数没减。** 前面说过，收益在别处。如果你要的是「造类别更省事」，那是另一件事，得从 `parseSpec` 返回值和消费者 API 的形状下手，不在本方案内。

**多一跳访问。** 作者代码里 `impl.x` 变 `def().x`。可以用宏抹平（像现在的 `stdc_impl_t`），但那是把成本藏起来不是消掉。

**一次性的公开接口破坏。** wolf 和 SVS 两个类别都要改。现在做代价最小——**内测期，没有外部使用者**；等有了第三方类别再做就得带兼容层。

**`Definition` 的稳定性承诺。** 拆出来之后它就是有承诺的公开接口了，加虚函数要考虑 ABI。这正是拆的目的（`Contribute.h` 不再背这个包袱），但要意识到包袱只是换了地方，没有消失。

---

## 十、不在本方案内

- **`class` → `interface` 改名**、**`kind` 的引入**：正交，可并行。
- **imports 泛化成 locator**：见 [singer-imports-plan.md](singer-imports-plan.md)。批 3 改 `parseSpec` 签名时会碰到同一批文件，**建议两者不要同时进行**。
- **`noLoad` 真的变懒**：`id` 进 desc.json 之后才有可能，但要单独评估谁在乎。
