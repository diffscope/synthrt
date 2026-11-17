# 契约与变体分离的计划

状态：已与作者确认方向，尚未实施。

## 动机

DS Spec 2.4 规定，`interface` 与 `level` 构成契约，契约规定 exports、imports 与运行期参数的词汇。`variant` 区分同一契约下的不同实现，`configuration` 的全部语法与格式版本由 `variant` 规定（`docs/ds-spec-2.4.md` 第 512 至 599 行）。

现行代码没有按这一划分组织。`ContribSpecPayload` 要求每个 payload 携带与目标模块完全一致的三元组（`docs/DevHelp.md` 第 97 行），因此属于契约的 exports、import options 与 runtime options 也必须知道 variant。dsinfer 的契约头文件因此写死了 `API_VARIANT`，并把 ONNX 实现的 configuration 与契约放在同一个文件中。wolf 的多变体契约无法写死，只能把 variant 作为构造参数传入，例如 `OnsetImportOptions options(variant)`。wolf 的 S2p 契约头文件还点名了具体实现（`VARIANT_DIRECT` 与 `variantKeepsSymbols()`）。

## 决定

### 加载规则

属于契约的 payload 只携带契约身份，即 `interface` 与 `level`。只有 configuration 携带完整的三元组。`PackageLoader` 的身份检查相应放宽：契约 payload 只核对 `interface` 与 `level`，configuration 核对三项。`docs/DevHelp.md` 第 97 行的规范文字随之修改。

### 类型

`synthrt/Core/ContribTriplet.h` 增加两个编译期类型：

- `srt::StaticContribContract`：`interfaceId` 与 `level`，表示一个契约。
- `srt::StaticContribTriplet`：一个 `StaticContribContract` 加上 `variant`，表示契约的一种实现。

两者都可以与运行期的 `srt::ContribTriplet` 直接比较，比较不分配内存，并可在需要拥有字符串时转换为 `ContribTriplet`。`ContribSpecPayload` 按 payload 的种类接受其中一种身份。

### 文件与目录结构

一个契约一个头文件，一个公开的变体一个子目录：

```
<库>/include/<库>/Api/<类别>/<契约>/<level>/
    <契约>ApiL<level>.h                    契约：Contract 常量、exports、import 参数、运行期参数、
                                          初始化参数、输入、结果、executive
    <变体>/<契约><变体>ApiL<level>.h        变体：Triplet 常量、Configuration
```

| 契约 | 契约头文件 | 变体头文件 |
|---|---|---|
| dsinfer 的 Acoustic、Duration、Pitch、Variance、Vocoder | `Inferences/<契约>/1/<契约>ApiL1.h` | `Inferences/<契约>/1/Onnx/<契约>OnnxApiL1.h` |
| dsinfer 的 DiffSinger 歌手 | `Singers/DiffSinger/1/DiffSingerApiL1.h` | `Singers/DiffSinger/1/OpenVpi/DiffSingerOpenVpiApiL1.h` |
| wolf 的 Linguist | `Linguists/Linguist/1/LinguistApiL1.h` | `Linguists/Linguist/1/Wolf/LinguistWolfApiL1.h` |
| wolf 的 G2p、S2p、Onset，otter 的全部契约 | 只有契约头文件 | 无公开的变体头文件 |

只有被其他模块读取的 configuration 需要公开的变体头文件。dsinfer 的 configuration 被 diffsinger provider、vocoder 与命令行工具读取，所以公开。wolf 与 otter 的 configuration 只由插件自己读取，定义在插件内部，Triplet 常量也放在插件的头文件中，取代现在的 `VARIANT`、`RULE` 等常量。

### 命名

- 命名空间已经给出契约名，类名去掉契约前缀，例如 `ds::Api::Acoustic::L1::AcousticImportOptions` 改为 `ds::Api::Acoustic::L1::ImportOptions`，变体的 configuration 为 `ds::Api::Acoustic::L1::Onnx::Configuration`。其余类同理。
- 常量改用大驼峰：契约头文件中的 `Contract`，变体头文件中的 `Triplet`。宏仍然全大写。例外是本身即为全大写缩写的标识，例如 `IID` 与 `ID`，以及枚举值，例如 `Backend::CPU`、`CUDA`、`DML`。`docs/Development.md` 中「宏与常量全部大写」的规则随之改写。

### wolf 的 S2p

删除契约头文件中的 `VARIANT_DIRECT` 与 `variantKeepsSymbols()`。一个 S2p 变体是否保留符号，改由该变体自身表达，例如写入它的 exports。wolflinguist 改为读取这一信息。

### 待定

dsinfer 的 `OnnxDriverApi` 以 `API_NAME` 与 `API_VERSION` 标识驱动 API，它不是 contribution，不使用三元组。按新的命名规则改名，或者结构化为一个名称与版本的类型，留待实施时决定。

## 实施

常量改名与三元组重构改动的是同一批代码：`API_INTERFACE`、`API_LEVEL`、`API_VARIANT` 在 synthrt、wolf、otter 中共约 300 处，重构后由 `Contract` 与 `Triplet` 取代。两者一次完成，不先单独改名。
