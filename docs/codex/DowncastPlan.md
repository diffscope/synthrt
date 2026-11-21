# 向下转型的身份校验

## 背景

`SYNTHRT_DECLARE_AS_METHODS` 给每个多态基类提供 `as<T>()`。它是 `static_cast`，正确性依赖调用方先比较对象携带的身份。debug 构建已经加了 `dynamic_cast` 断言，但那只在测试里兜底，release 仍然靠约定。

身份在每个家族里的形状并不一样：

| 家族 | 身份访问器 |
|---|---|
| `TaskPayload`、`ContribSpecPayload`、`InferenceDriverPayload` | `contract()` |
| `ContribSpec` | `triplet()` |
| `ContribCategory` | `triplet()` |
| `ContribImportBinding` | `declaration()` |
| `ContribSpecExtension` | `id()` |
| `RuntimeService` | `iid()` 与名字 |
| `ContribInterpreter`、`ContribExecutive` | 由创建它们的三元组决定，对象自身不携带 |

所以「给每个家族补一个 `asChecked(expected)`」不是一个能收敛的做法：签名不同、比较类型不同，每加一个家族就要复制一次。这一轮试过两个家族（`TaskPayload`、`InferenceDriverPayload`），补丁保存在 `.cache/codex/2026-11-21-aschecked-wip.patch`，没有提交。

## 目标

1. 一处实现，所有带身份的家族共用。
2. 调用点不可能「比较 A 的契约，转型成 B」。
3. release 构建不引入 RTTI 依赖。
4. 代价集中在声明身份的地方，不动使用方。

## 候选方案

### B. 统一身份访问器加目标声明身份（推荐）

每个家族基类提供一个 `identity()`：

- payload 家族返回 `contract()` 的值。
- `ContribSpec` 与 `ContribCategory` 返回 `triplet()`。
- `ContribImportBinding` 返回 `declaration()`。
- `ContribSpecExtension` 返回 `id()`。
- `RuntimeService` 返回一个 `{iid, name}` 视图。

每个派生类型声明自己的身份：

```cpp
class InitArgs : public srt::InferenceInitArgs {
public:
    inline static const srt::ContribContract Identity = Api::Contract;
    // ...
};
```

`SYNTHRT_DECLARE_AS_METHODS` 增加一个不需要参数的检查版本：

```cpp
template <class T>
std::optional<T *> asChecked() {
    static_assert(DowncastIdentity<T>::declared, "T must declare Identity");
    if (identity() != DowncastIdentity<T>::of()) {
        return std::nullopt;
    }
    return as<T>();
}
```

比较发生在同一个家族内，两边的身份类型相同，不需要跨类型的 `operator==`。

返回 `std::optional` 而不是 `Expected`，因为 `synthrt_global.h` 不能包含 `Expected.h`：后者经 `Error.h` 依赖前者，会成环。需要错误消息的调用点可以保留一个带 expected 参数的变体，或者用 `identity().toString()` 自己拼。

代价：约 40 个类型要加 `Identity`，8 个基类要加 `identity()`。机械、一次性。

### C. 规范化字符串身份（过渡）

每个家族提供 `identityString()`，派生类型声明一个字符串常量，比较字符串。实现最简单，但每次转型构造字符串，拼写错误无法编译期发现。只适合作为诊断输出，不适合当主机制。

### D. 类型注册表（长期）

`QMetaType` 与 `protobuf Any` 式的方案：注册 factory、clone、serialize、describe，转型变成注册表查找。它解决的是跨进程、持久化与通用操作，不是为了 cast 安全。等有 IPC 或缓存需求时再做。

## 迁移顺序

1. 引入 `DowncastIdentity` trait 与 `identity()` 约定。
2. 先迁移三个 payload 家族，它们占现有边界调用点的大多数。
3. 再迁移 spec、category、binding、extension、service。
4. 把「先比较、再 `as<T>()`」的调用点换成 `asChecked<T>()`。
5. `as<T>()` 保留给框架已经校验过的内部路径。

## 未决问题

- `RuntimeService` 的身份是 iid 加名字，需要定义一个小视图并参与比较。
- `ContribInterpreter` 与 `ContribExecutive` 自身不携带身份，需要由工厂或规格传入。这是设计变更，要单独讨论。
- 每个 API 头多一行 `Identity` 声明，可以用宏压缩，但要先看样板量。
