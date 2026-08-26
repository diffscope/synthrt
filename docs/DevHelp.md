# SynthRT 与 dsinfer 开发指南

本文说明当前仓库如何实现 [DiffSinger 数据格式与推理接口规范 2.4](ds-spec-2.4.md)，以及宿主程序、契约开发者、Contribution Category 扩展者和插件开发者应当如何使用现有 API。规范仍是清单格式与行为约束的权威来源，本文关注代码中的对象关系、调用顺序、所有权和扩展点。

## 1. 整体模型

SynthRT 把“声明是什么”和“运行它”分成两层：

1. Package 是安装后的目录。根目录包含 `desc.json`。
2. Contribution 是 Package 提供的一项能力。它由 category 和 Package 内局部 ID 标识。
3. `ContribSpec` 是 Contribution 声明的不可变内存对象。
4. Module Category 的 Contribution 还具有 `interface`、`level`、`variant` 三元组，并由解释器把 JSON 解释成类型化对象。
5. `ContribExecutive` 是由已加载 Contribution 创建的运行时执行对象。
6. `RuntimeService` 是 SynthUnit 级别的共享后端，例如 dsinfer 的 ONNX Inference Driver。它不是 Contribution。

主要关系如下：

| 对象 | 作用 | 所有者 | 典型生命周期 |
|---|---|---|---|
| `SynthUnit` | Category、Package、解释器和 Runtime Service 的运行时边界 | 宿主 | 最长 |
| `ContribCategory` | 解析和索引一种 Contribution | `SynthUnit` | 不得晚于任何 Package |
| `RuntimeService` | 为多个 Executive 提供共享进程资源 | `SynthUnit` | 不得晚于使用它的 Executive |
| `PackageHandle` | 对 Package 的共享强引用 | 调用者和 dependency edge | 短于 `SynthUnit` |
| `ContribSpec` | 一个 Contribution 的不可变声明 | Package | 与 Package 相同 |
| `ContribInterpreter` | 解释一个三元组的清单并创建运行时对象 | `SynthUnit` 内部插件工厂 | 插件加载后常驻 |
| `ContribImportBinding` | 一个 import role 到目标 Contribution 的连接 | importing `ContribSpec` | 与 importing Package 相同 |
| `ContribExecutiveFactory` | 经某个 import 创建目标 Executive | 对应 `ContribImport` | 与 importing Package 相同 |
| `ContribExecutive` | 实际运行工作并监督子 Executive | 根 Executive 由调用者持有，子 Executive 由父 Executive 持有 | 短于所属 Package |

`SynthUnit` 不是全局单例。一个进程可以创建多个 SynthUnit，它们具有各自的 Category 实例、搜索路径、已加载 Package、Runtime Service 和解释器缓存。

## 2. 2.4 Package 如何映射到对象

### 2.1 `desc.json`

一个最小的 Module Package 可以写成：

```json
{
  "$version": "1.0",
  "id": "org.example.voice",
  "version": "1.2.0.0",
  "compatVersion": "1.0.0.0",
  "runtimeLevel": 1,
  "name": "Example Voice",
  "contributions": {
    "singer": [
      {
        "id": "alice",
        "path": "singers/alice.json"
      }
    ],
    "inference": [
      {
        "id": "acoustic",
        "path": "inferences/acoustic.json"
      }
    ]
  },
  "dependencies": [
    {
      "id": "org.example.runtime-assets",
      "version": "1.0.0.0"
    }
  ]
}
```

当前 Loader 只接受已经安装完成的 Package 目录，不直接加载 `.dspk`。`PackageHandle::manifestDeclaration()` 返回完成字符串变量展开后的根对象，并保留规范未占用的未知字段。

`PackageHandle` 提供 Package 身份、显示信息、路径、依赖、原始清单和 Contribution 查询。`PackageHandle::contributions(category)` 返回该类别的所有声明，`contribution(category, id)` 查询一个本地声明，`resolve(locator)` 则可以沿已经绑定的直接 dependency edge 查找目标。

### 2.2 Module 声明

Module Category 的条目使用 `path` 指向声明文件。公共 envelope 为：

```json
{
  "name": "Acoustic Model",
  "interface": "org.openvpi.dsinfer.inference.Acoustic",
  "level": 1,
  "variant": "openvpi",
  "exports": {},
  "configuration": {},
  "imports": [
    {
      "role": "singer/vocoder",
      "ref": ":inference/vocoder",
      "options": {}
    }
  ]
}
```

`ContribSpec` 保存公共字段和模块声明文件的 `declarationPath()`。解释器加载前，`manifestExports()`、`manifestConfiguration()` 和 `ContribImport::manifestOptions()` 提供展开后的 JSON。成功 Load 后，`exports()`、`configuration()` 和 `ContribImport::options()` 提供解释器创建的类型化对象。

`ContribSpecPayload` 是这些类型化对象的公共身份基类。每个派生对象必须携带与目标 Module 完全一致的 `interface`、`variant` 和 `level`。Loader 会检查这组三元组，错误或空返回值会使整个加载事务失败。

### 2.3 Locator、Dependency 与 role

`ContribLocator` 对应规范中的 ModuleReference：

```text
:inference/acoustic
org.example.models:inference/acoustic
```

第一种形式引用当前 Package，第二种形式引用 `dependencies` 已经选定的直接依赖。Locator 不包含版本，也不会触发第二次依赖搜索。

每个 import 使用唯一 `role` 标识自己的职责。Role 可以使用以`/`分隔的多个 segment，建议由扩展先使用首段标识自己的 role family，例如`singer/acoustic`。相同 `ref` 可以在多个 role 中重复出现，每个条目仍有独立的 options、binding 和 Executive factory。代码应使用 `ContribSpec::findImport(role)` 定位 import，不依赖数组位置。

### 2.4 版本、变量、路径和多语言字段

当前实现支持清单格式 `$version` 1.0 和 Runtime Level 1。`desc.json` 必须显式写 `runtimeLevel: 1`。Package `compatVersion` 不能高于 `version`，缺省时等于 `version`。

`vars` 是按顺序求值的字符串变量数组：

```json
{
  "vars": [
    {"name": "assets", "value": "${root}/assets"},
    {"name": "models", "value": "${assets}/models"}
  ]
}
```

Package 变量对全部声明可见，Module 变量只在自己的声明中可见并可覆盖 Package 同名变量。每项只能引用外层变量和同一数组中更早的变量。`${root}` 是 Package 根目录，`${dir}` 是当前声明文件目录，`$$` 产生字面 `$`。缺失或非法变量名展开为空。Loader 会在 Category 和解释器看到 JSON 前统一完成展开，扩展代码不得再次展开字符串。

变量只产生字符串，不携带路径基准。一个路径字段要先完成整个字符串展开，再把相对结果解析到该字段所在声明文件的目录。绝对路径直接使用。规范允许 `..` 和 Package root 外部路径，Package 发布者负责保证这些外部布局在同一 Package 身份和整个使用期间保持一致。

`name`、`description` 等显示字段可以是字符串，也可以是含 `_` 默认值的多语言 map。Runtime 不解释语言代码。`DisplayText::text()` 读取默认值，`locales()` 和 `text(locale)` 允许前端读取并选择本地化值。`readme`、`avatar`、`background` 和 `demoAudio` 是多语言路径，各语言值都会按自己的声明文件目录解析。

Package ID、category、Contribution ID、interface 和 variant 都是区分大小写的 ASCII 结构标识符。不要对它们执行 locale 大小写转换或 Unicode normalization。

## 3. 宿主初始化

初始化顺序必须是：

1. 确保所有静态 Category 已经完成注册。
2. 构造需要比插件对象活得更久的插件 Factory。
3. 构造 `SynthUnit`。
4. 手动添加额外 Category 和 Runtime Service。
5. 设置每个 Category 的解释器搜索路径和 Package 搜索路径。
6. 第一次调用 `openPackage()`。

第一次打开 Package 后，当前 SynthUnit 不再接受新的 Category 或 Runtime Service。`DataOnly` 也算开始了 Package 打开过程，因此不能先用同一个 SynthUnit 做 DataOnly，然后再补注册项。

### 3.1 Category 注册

链接时已知的 Category 可以加入进程级 `ContribCategoryRegistry`：

```cpp
static srt::ContribCategoryRegistry::Add<MyCategory> myCategoryRegistration(
    "com.example.language", "");
```

`SynthUnit` 构造时会为 Registry 中的每一项创建一个全新 Category 实例。`inference` 和 `singer` 由 synthrt 使用这种方式注册。

应用程序也可以在打开第一个 Package 前显式注册：

```cpp
srt::SynthUnit unit;

auto result = unit.addCategory(std::make_unique<MyCategory>());
if (!result) {
    // 处理 result.error()
}
```

Category 名称在一个 SynthUnit 内唯一。两个 Module Category 也不能复用同一个解释器插件 IID，因为 IID 决定该 Category 的独立插件候选集合。

### 3.2 插件和 Package 搜索路径

每个 Module Category 有独立的有序插件搜索路径：

```cpp
unit.setPluginPaths("singer", {pluginRoot / "singerproviders"});
unit.setPluginPaths("inference", {pluginRoot / "inferenceinterpreters"});
unit.setPackagePaths({installedPackagesA, installedPackagesB});
```

插件路径的直接子目录是 bundle。每个 bundle 包含 `plugin.json` 和由 `name` 定位的动态库。插件 IID 嵌入动态库，`plugin.json` 只保存该扩展点拥有的用户 metadata。Provider 发现会读取动态库中的 IID 和 sidecar metadata，但不会执行插件代码。

无法读取或不符合该 Category metadata 结构的 bundle 不进入候选集合。找到首个三元组匹配项后，后续 Provider 不再参与本次选择。若选中的动态库无法加载，或者解释器随后拒绝 configuration，整个 Load 失败，不会回退到下一个插件。

Package 搜索路径顺序高于版本。在第一个含有兼容候选的搜索路径中，Loader 选择最高版本。选择一旦作出，后续 Probe、Acquire 或 Ready 失败都不会回退到旧版本或后续路径。

### 3.3 dsinfer Inference Driver

Inference Driver 是 Runtime Service，不是 Contribution，也不通过 `inference` Category 的解释器 Factory 创建。典型初始化为：

```cpp
ds::InferenceDriverFactory driverFactory;
srt::SynthUnit unit;

driverFactory.addPluginPath(pluginRoot / "inferencedrivers");
auto driverResult = driverFactory.create(ds::Api::Onnx::API_NAME);
if (!driverResult) {
    return driverResult.takeError();
}

auto driver = driverResult.take();
ds::Api::Onnx::DriverInitArgs args;
args.ep = ds::Api::Onnx::ExecutionProvider::CPU;
args.runtimePath = onnxRuntimeDirectory;
if (auto result = driver->initialize(args); !result) {
    return result.takeError();
}

if (auto result = unit.addRuntimeService(std::move(driver)); !result) {
    return result.takeError();
}
```

`InferenceDriverFactory` 持有已加载 Driver 插件的代码，必须比它创建的 Driver 活得久。上例中应先构造 Factory，再构造 SynthUnit，使销毁顺序为 SynthUnit 在前、Factory 在后。SynthUnit 接管 Driver 的对象所有权。

Driver bundle 同样把 `InferenceDriverPlugin::IID` 嵌入动态库。其 `plugin.json` 只声明用于定位动态库的 `name` 和用于选择 Driver 的 `backend`：

```json
{
  "name": "onnxdriver",
  "backend": "onnx"
}
```

Executive 可以通过 `synthUnit().runtimeService(InferenceDriverPlugin::IID, backend)` 找到共享 Driver。IID 属于创建 Driver 的插件接口，Driver 类型本身不另行声明 IID。Driver 再创建 `InferenceSession`，Session 是 `ITask`，负责打开一个模型并执行同步或异步推理。

## 4. 打开模式

### 4.1 DataOnly

```cpp
auto result = unit.openPackage(packageDirectory, srt::SynthUnit::DataOnly);
```

DataOnly 会读取 `desc.json` 和 Category 所需的声明文件，执行变量展开，并调用 Category 的 `createSpec()` 生成类型化 Spec。它不会发现插件、加载插件、创建解释器、解析契约 payload、创建 binding，也不会把 Package 发布到 `SynthUnit::loadedPackages()`。

DataOnly 返回的 `PackageHandle::isLoaded()` 为 false。Module Spec 的 `exports()`、`configuration()`、`ContribImport::options()`、`binding()` 和 `executiveFactory()` 都为空。原始 manifest 访问器仍可使用。

DataOnly 仍然要求清单里出现的 Category 已经注册，因为 Category 决定 Contribution entry 和声明如何构造成 Spec。

### 4.2 Load

```cpp
auto result = unit.openPackage(packageDirectory, srt::SynthUnit::Load);
if (!result) {
    return result.takeError();
}
auto package = result.take();
```

Load 完成依赖闭包、解释器加载和所有验证，最后才返回已 Commit 的 `PackageHandle`。失败时不会返回半加载 Handle。

如果相同 `id` 和规范化版本的 Package 已经 Commit，Loader 直接返回指向现有实例的新 Handle，不重新解析、重新选择解释器或重新初始化。

## 5. Load 事务

当前实现用 SynthUnit 内部递归互斥量串行化 Package 状态变化，并按 Probe、Acquire、Ready、Commit 四个阶段工作。

### 5.1 读取候选

Loader 先读取根 Package，再扫描 Package 搜索路径的直接子目录。候选预读只保留决定依赖兼容性所需的信息。无效目录不会成为候选，同一搜索路径内出现相同 Package 身份则报重复错误。

Dependency 的 `version` 是兼容目标。候选必须满足：

```text
candidate.compatVersion <= dependency.version <= candidate.version
```

不同版本可以同时 Commit。每个 dependency ID 在一个 Package 中只能绑定一个已选 Package 实例。

### 5.2 Probe

Probe 不执行 Provider 代码，主要完成：

1. 深度优先解析依赖并拒绝依赖环。
2. 固定每条 dependency edge 的 Package 版本。
3. 构造所有 Category Spec。
4. 解析每个 Locator，并确认目标存在且属于 Module Category。
5. 从目标 Category 的插件元数据中为每个三元组选择第一个 Provider。

插件选择键为 Category 所属插件 IID 加 `interface`、`level`、`variant`。同一个目录中的 bundle 由 stdcorelib.plugin 提供确定顺序，搜索路径按宿主给出的顺序处理。选中的 PluginLoader 会记录在 Spec 上，后续不会因为搜索路径改变而改选。

### 5.3 Acquire

Acquire 才真正加载 Provider 插件。一个已加载插件中的同一个三元组只创建并缓存一个 `ContribInterpreter`，它可以服务多个 Package 和 Spec。

对每个新 Module，Loader 依次调用：

1. `ContribInterpreter::createExports(spec)`。
2. `ContribInterpreter::createConfiguration(spec)`。
3. 校验返回 payload 非空且三元组与 Spec 一致。

因此即使一个 Module 从未被 import，它自己的 exports 和 configuration 也必须有效，否则整个事务失败。

### 5.4 Ready

所有新 Module Acquire 成功后，Loader 对每个 import 执行：

1. 用目标 Module 的解释器调用 `createImportOptions(target, manifestOptions)`。
2. 校验 options payload 的三元组与目标 Module 一致。
3. 用 importing Module 的解释器调用 `createImportBinding(importer, declaration, target, options)`。
4. 用目标 Category 调用 `createExecutiveFactory(binding)`，把目标 Category 的运行时创建能力挂到该 role。

这里有意把职责分开：目标解释器最懂自己的 options 契约，importing 解释器最懂多个 role 如何组合，目标 Category 最懂如何从目标 Spec 创建哪种 Executive。

Ready 产生的 Binding 处于 `Prepared`，不得开始业务执行。所有可能失败、分配或执行 I/O 的准备工作都必须在 Commit 前完成。

Ready 内部明确分为三个 pass。解释器首次创建时可以通过 `createImportValidators()` 向当前 SynthUnit 提供零个或多个 Import Validator。第一遍为整个事务中的全部 import 创建 Binding 和 Executive Factory。第二遍让每个 `ContribImportValidator` 检查所有 Spec 已经准备好的 options、Binding、Executive Factory 以及跨 import 兼容性。第三遍对每个 Spec 调用每个已选中解释器的 `createExtensions(spec)`，解释器不扩展该 Spec 时返回空 vector，适用时可以一次返回一个或多个 Extension。Import Validator 与 Extension 创建是相互独立的职责，一个验证器不必创建 Extension。每个 Extension 均由对应 Spec 持有，其 ID 在同一 Spec 内必须唯一。验证失败、创建失败、返回空指针、ID 非法或重复都会使整个 Load 失败。

Extension 是加载后附加到 Spec 的类型化能力，不是新的 Contribution，也不改变原 Spec 的清单身份。DataOnly 不创建 Extension。Extension 可以保存对 Spec、Binding 和 Executive Factory 的非拥有引用，但不能在 Commit 前启动业务执行。Extension 在其引用的 import 数据之前销毁。

### 5.5 Commit

Commit 只执行不可失败的发布操作：

1. 把新 Package 放入 SynthUnit 的弱索引。
2. 把 Spec 放入对应 Category 的已提交索引。
3. 调用每个 Binding 的 `activate()`，状态从 `Prepared` 进入 `Active`。
4. 把整个事务中的新 Package 标记为 loaded。

`ContribImportBinding::activate()` 必须是 `noexcept`，不得分配、执行 I/O 或失败。Commit 前没有新对象对运行时读取者可见，Commit 后整个依赖闭包统一可见。

### 5.6 失败和 rollback

Probe、Acquire 或 Ready 的任一步失败都会返回原始错误及逐层 context。事务内对象由所有权结构反向销毁，尚未激活的 Binding 不执行正常运行期 close。已经 Commit 且只是被本事务临时引用的 Package 不会被重新初始化或撤销。

Provider 插件一旦成功加载便由 SynthUnit 内部插件 Factory 常驻持有。某个 Package 加载失败或卸载不会卸载插件，也不会销毁其他 Package 正在共享的解释器。

## 6. 引用计数和卸载

### 6.1 Package 强引用

`PackageHandle` 内部使用共享所有权。复制 Handle 会增加强引用，`reset()` 或析构会减少强引用。已 Commit Package 对每个直接 dependency 持有一个强引用，SynthUnit 的 Package registry 只持有弱引用。

这意味着根 Package 的最后一个 Handle 释放后：

1. 根 Package 若没有其他引用便开始析构。
2. 它从 Category 索引中移除自己的 Spec。
3. 它关闭并等待所有 ImportBinding。
4. 它释放 dependency edge。
5. 不再被其他 Handle 或 Package 使用的依赖可递归析构。

再次打开一个仍被引用的已 Commit Package，只取得新 Handle，不会重复加载。

### 6.2 Executive 必须先销毁

当前实现不会在最后一个 PackageHandle 析构时替调用者保存并销毁根 Executive。每个 `ContribExecutive` 构造时都会登记到所属 Package。若 Package 析构时仍存在任何 Executive，运行时会 fatal，而不是留下悬空 Spec 或继续卸载。

正确顺序为：

```cpp
pipeline.reset(); // 同时销毁其监督的全部子 Executive
package.reset();
// 最后才销毁 SynthUnit
```

所有从 `PackageHandle::contribution()`、`contributions()`、`resolve()` 或 `ContribCategory::contributions()` 得到的裸指针也必须在保活该 Package 的 Handle 释放前停止使用。

### 6.3 Executive 监督树

根 Executive 通常由调用者持有 `std::unique_ptr`。父 Executive 使用 `adoptChild()` 接管子 Executive，或者使用 `createChild(role, runtimeOptions)` 经 import 上的 Factory 创建并接管子 Executive。

父 Executive 析构时会删除全部子 Executive。调用者也可以提前 `delete` 一个由父 Executive 返回的子指针，子对象会先从父对象的 children 列表中脱离。不得把子指针包装进第二个 owning smart pointer。

Executive 具有 `Running`、`Stopping`、`Stopped` 生命周期状态。卸载协议要求先 quit 所有入口，再 wait 到静止。当前顶层 Package API 通过“所有 Executive 必须先由调用者销毁”保证这一点，具体 Executive 和其中 Task 的析构函数必须停止并等待自己的执行活动。

`ContribExecutive` 基类析构函数不会调用虚 `quit()` 或 `wait()`。C++ 基类析构期间也不能再安全分派到派生实现。因此具体 Executive 必须在自己的析构过程或成员对象析构过程中完成停止和等待。当前 dsinfer Executive 由内部 Task 析构函数完成这道屏障。

### 6.4 Binding 生命周期

Binding 状态为：

```text
Prepared -> Active -> Closed
```

`activate()` 在 Commit 屏障内执行。正常卸载会先对 Package 的所有 Binding 调用 `close()`，阻止双方发起新调用，再对所有 Binding 调用 `wait()`，最后才销毁 Spec 和 dependency edge。自定义 Binding 必须使 close 不失败且不阻塞，把排空已有调用的工作放到 wait。

## 7. 解释器如何工作

### 7.1 插件声明与用户 Metadata

一个解释器插件可以支持多个三元组：

```json
{
  "name": "acoustic",
  "interpreters": [
    {
      "interface": "org.openvpi.dsinfer.inference.Acoustic",
      "level": 1,
      "variant": "openvpi"
    }
  ]
}
```

IID 由 Category 决定，并通过 `stdc_add_plugin_metadata()` 嵌入动态库，不写入 `plugin.json`。内置值为：

- inference：`org.openvpi.synthrt.plugin.InferenceInterpreter`
- singer：`org.openvpi.synthrt.plugin.SingerProvider`

`plugin.json` 本身就是 `PluginLoader::metadata()` 返回的用户 metadata，不再包含 `iid` 或额外的 `metadata` 包装层。`name` 是同目录动态库的跨平台中立名称。Loader 在 Probe 读取嵌入式 IID 和 sidecar metadata。Acquire 加载库后，调用 `ContribInterpreterPlugin::create(interface, level, variant)`。

插件目标应在所属 category 的 CMake 层设置 IID，并在创建目标后声明插件二进制：

```cmake
set(CURRENT_PLUGIN_IID org.openvpi.synthrt.plugin.InferenceInterpreter)

stdc_add_plugin_metadata(
    TARGET ${PROJECT_NAME}
    IID ${CURRENT_PLUGIN_IID}
)
```

本仓库的 dsinfer 插件通过 `dsinfer_add_plugin_metadata(target, iid)` 包装上述调用，并负责把同目录的 `plugin.json` 复制或安装到 bundle 输出目录。

插件加载成功后在 SynthUnit 内常驻，Package rollback 和卸载不会卸载它。SynthUnit 销毁时会先销毁 Package 弱索引、Category 和 Runtime Service，最后才释放内部插件 Factory 及其解释器和动态库。

### 7.2 `ContribInterpreter`

自定义解释器至少实现六个职责：

- `createExports()`：把目标 Spec 的 manifest exports 转成派生自 `ContribExports` 的对象。
- `createConfiguration()`：把 configuration 转成派生自 `ContribConfiguration` 的对象。
- `createImportOptions()`：为一个指向该契约的 import 解析 options。
- `createImportValidators()`：创建在 Ready 第二遍检查完整 Prepared import 图的验证器。
- `createImportBinding()`：建立 importing Spec 到目标 Spec 的 Prepared 连接。
- Category 或更具体的解释器接口提供实际 Executive 创建入口。

解释器可能被多个 Package 和 Spec 共享。不要在解释器对象里存放“当前 Spec”一类单实例状态。每次调用都以参数中的 Spec 为准，共享缓存必须明确同步并且不能破坏 Package 生命周期。

### 7.3 类型化 payload 与 `as<T>()`

`as<T>()` 最终是静态转换，不做 RTTI 检查。正确用法是先检查身份，再转换：

```cpp
if (spec.interface() != MyApi::Interface || spec.variant() != MyApi::Variant ||
    spec.level() != MyApi::Level) {
    return srt::Error(srt::Error::InvalidArgument, "unexpected contract");
}

auto configuration = spec.configuration()->as<MyConfiguration>();
```

Category 名称确定 `ContribSpec` 的派生类型，三元组确定 payload、解释器和 Executive 的派生类型。身份未验证时调用 `as<T>()` 是调用方错误。

## 8. ImportBinding、Executive Factory 与开放 role

### 8.1 为什么 Factory 挂在 import 上

一个 singer 的 imports 不限于固定的五种 inference。未来第三方 Category 可以增加 linguist、dictionary 或其他 role family。因而 SynthRT 不在 Singer 基类中硬编码所有可创建对象，而是在 Ready 时让每个 import 保存目标 Category 创建的 `ContribExecutiveFactory`。

运行时调用：

```cpp
auto import = spec.findImport("singer/acoustic");
if (!import || !import->executiveFactory()) {
    // role 不存在，或者目标 Category 没有运行时实例
}
```

Executive 派生类通常不直接暴露通用 Factory，而是提供契约类型化方法，内部调用受保护的 `createChild()`：

```cpp
srt::Expected<MyExecutive *> MyPipeline::createLanguage(
    const MyRuntimeOptions &options) {
    auto result = createChild("language", options);
    if (!result) {
        return result.takeError();
    }

    auto executive = *result;
    const auto &target = executive->spec();
    if (target.interface() != MyApi::Interface || target.variant() != MyApi::Variant ||
        target.level() != MyApi::Level) {
        delete executive;
        return srt::Error(srt::Error::InvalidFormat, "factory returned an incompatible Executive");
    }
    return executive->as<MyExecutive>();
}
```

`createChild()` 会查 role、调用 Factory、检查父子属于同一个 SynthUnit、防止监督环，并把成功创建的 Executive 所有权交给父 Executive。

### 8.2 谁创建 Factory

Loader 对每个 import 调用目标 Category 的 `createExecutiveFactory(binding)`。Factory 因而可以使用：

- `binding.target()` 获取目标 Spec。
- `binding.options()` 获取目标解释器解析后的 options。
- `binding.importer()` 获取发起导入的 Spec。
- `binding.declaration().role()` 获取 role。

Factory 的 `create(runtimeOptions)` 必须验证 runtime options 的三元组，并返回属于 binding target 的 Executive。内置 `InferenceCategory` 的 Factory 会调用 `InferenceSpec::createInference()`，然后确认返回 Executive 的 `spec()` 正是目标 Spec。

如果一种 Category 没有运行时对象，保持 `createExecutiveFactory()` 的默认实现即可。此时 Factory 为空，调用该 role 创建子 Executive 会返回 `FeatureNotSupported`。

## 9. dsinfer 的内置执行模型

### 9.1 Inference

`InferenceCategory` 把 inference entry 构造成 `InferenceSpec`，并使用 `InferenceInterpreterPlugin` 选择插件。具体契约 API 位于 `dsinfer/include/dsinfer/Api/Inferences`。

每个契约定义自己的：

- `API_INTERFACE`、`API_VARIANT`、`API_LEVEL`
- exports 类型
- configuration 类型
- import options 类型
- runtime options 类型
- init、start 和 result payload
- 派生自 `InferenceExecutive` 的类型化 Executive 接口

`InferenceExecutive` 不公开一个通用 `ITask&`。它只公开通用状态、停止和等待。Acoustic、Duration、Pitch、Variance 和 Vocoder Executive 各自公开类型化 `initialize()`、`start()` 和 `startAsync()`，调用者不能绕过契约传入任意 `TaskPayload`。

解释器创建具体 Executive，具体 Executive 可以在内部组合一个或多个 `ITask`。当前 dsinfer 的每种 inference Executive 组合一个契约专用 Task，Task 再通过 SynthUnit 的 Inference Driver Runtime Service 创建模型 Session。

### 9.2 Inference 兼容性

`InferenceSpec::validateCompatibilityWith(other)` 是有方向的检查，由当前 Spec 的解释器执行。默认 `InferenceInterpreter::validateCompatibility()` 接受所有组合，只有存在跨模型约束的契约需要覆盖。

当前 DiffSinger Import Validator 要求`singer/acoustic`和`singer/vocoder` role 存在，并调用 vocoder 的兼容性检查验证其能否消费 acoustic 输出。该检查发生在 Ready 第二遍，此时所有 import 已经具有 Binding 和 Executive Factory。因此不兼容 Package 会在 Load 阶段失败，而不是运行到一半才失败。

### 9.3 Singer Pipeline

Singer Pipeline 由 `SingerPipelineExtension` 创建，不再由 `SingerSpec` 独占创建。Loader 在 Ready 的最后一遍调用每个已选中 Interpreter 的 `createExtensions(spec)`。例如 DiffSinger Provider 可以为自身契约的 Singer 提供 Pipeline，Wolf Linguist Provider 也可以为导入 Linguist Contribution 的 Singer 提供独立 Pipeline。解释器不适用时返回空 vector，适用时按需附加一个或多个 Pipeline Extension。返回的 `SingerPipelineExecutive` 是根 Executive，它通过已经聚合的 import role 创建子 Executive。

DiffSinger Level 1 的类型化 Pipeline 当前公开 duration、pitch、variance、acoustic 和 vocoder 创建函数。对应 role 分别为`singer/duration`、`singer/pitch`、`singer/variance`、`singer/acoustic`和`singer/vocoder`。`DiffSingerImportValidator` 将 acoustic 和 vocoder 视为必需 role，其他三个可以省略。调用一个清单未声明的可选 role 时，创建函数会返回错误，调用者必须按自己的工作流决定是否需要它。

典型调用为：

```cpp
auto base = package.contribution("singer", "alice");
if (!base) {
    // 未找到 singer
}

auto singer = base->as<srt::SingerSpec>();
if (singer->interface() != ds::Api::DiffSinger::L1::API_INTERFACE ||
    singer->variant() != ds::Api::DiffSinger::L1::API_VARIANT ||
    singer->level() != ds::Api::DiffSinger::L1::API_LEVEL) {
    // 不是调用方理解的契约
}

auto extension = srt::ContribSpecExtension::findFromSpec<
    ds::Api::DiffSinger::L1::DiffSingerPipelineExecutive>(*singer);
if (!extension) {
    // 当前 Singer 没有 DiffSinger Pipeline
}

auto pipelineResult = extension->as<srt::SingerPipelineExtension>()->createPipeline(
    ds::Api::DiffSinger::L1::DiffSingerPipelineRuntimeOptions());
if (!pipelineResult) {
    return pipelineResult.takeError();
}
auto pipeline = pipelineResult.take();
auto typedPipeline =
    pipeline->as<ds::Api::DiffSinger::L1::DiffSingerPipelineExecutive>();

auto acousticResult = typedPipeline->createAcoustic(
    ds::Api::Acoustic::L1::AcousticRuntimeOptions());
if (!acousticResult) {
    return acousticResult.takeError();
}
auto acoustic = *acousticResult; // 由 pipeline 持有
```

调用者持有 `pipeline`，但不持有 `acoustic`。销毁 Pipeline 会销毁所有仍被监督的子 Executive。

## 10. 扩展新的 Module Category

下面给出完整职责清单。示例名称使用 `com.example.language`。

### 10.1 定义 Category 和 Spec

```cpp
class LanguageSpec : public srt::ContribSpec {
private:
    explicit LanguageSpec(const srt::ContribCreateContext &context)
        : ContribSpec(context) {
    }

    friend class LanguageCategory;
};

class LanguageCategory : public srt::ContribCategory {
public:
    LanguageCategory()
        : ContribCategory("com.example.language", ModuleDeclaration,
                          LanguageInterpreterPlugin::IID) {
    }

protected:
    srt::Expected<std::unique_ptr<srt::ContribSpec>>
        createSpec(const srt::ContribCreateContext &context) const override;

    srt::Expected<std::unique_ptr<srt::ContribExecutiveFactory>>
        createExecutiveFactory(srt::ContribImportBinding &binding) const override;
};
```

`createSpec()` 在插件发现前执行，只能解析 Category 自己拥有的 entry 字段和公共 Module envelope。不要在这里加载插件、模型或设备。返回的 Spec 必须以收到的 `ContribCreateContext` 构造，不能保存 Context 指针，因为 Context 只在本次调用期间有效。

### 10.2 定义契约 payload 和 Executive

为每个公开契约定义固定三元组，并让所有类型化 payload 调用对应基类构造函数：

```cpp
namespace LanguageApi::L1 {

inline constexpr char Interface[] = "com.example.language.Frontend";
inline constexpr char Variant[] = "rules";
inline constexpr int Level = 1;

class Exports : public srt::ContribExports {
public:
    Exports() : ContribExports(Interface, Variant, Level) {
    }
};

class Configuration : public srt::ContribConfiguration {
public:
    Configuration() : ContribConfiguration(Interface, Variant, Level) {
    }
};

class ImportOptions : public srt::ContribImportOptions {
public:
    ImportOptions() : ContribImportOptions(Interface, Variant, Level) {
    }
};

class RuntimeOptions : public srt::ContribRuntimeOptions {
public:
    RuntimeOptions() : ContribRuntimeOptions(Interface, Variant, Level) {
    }
};

class Executive : public srt::ContribExecutive {
public:
    virtual srt::Expected<std::string> process(std::string_view text) = 0;

protected:
    using ContribExecutive::ContribExecutive;
};

}
```

如果 Executive 内部有异步执行，必须实现 `quit()` 和 `wait()`，保证停止后不再访问 Package、Spec、Binding 或 Runtime Service。若只做同步工作，两者可以立即成功。

当前实现要求调用者在 Package 释放前主动销毁根 Executive，所以具体 Executive 的析构函数仍必须自行触发停止和等待。不能只实现两个虚函数却假设基类析构会调用它们。

### 10.3 定义 Binding 和 Factory

```cpp
class LanguageBinding : public srt::ContribImportBinding {
public:
    using ContribImportBinding::ContribImportBinding;

protected:
    void activate() noexcept override {
        // 只发布 Ready 已准备好的状态
    }

    void close() noexcept override {
        // 原子阻止新调用
    }

    srt::Expected<void> wait() override {
        // 等待已经进入的调用离开
        return {};
    }
};

class LanguageExecutiveFactory : public srt::ContribExecutiveFactory {
public:
    explicit LanguageExecutiveFactory(srt::ContribImportBinding &binding) : m_binding(&binding) {
    }

    srt::Expected<std::unique_ptr<srt::ContribExecutive>>
        create(const srt::ContribRuntimeOptions &options) override;

private:
    srt::ContribImportBinding *m_binding;
};
```

Category 的 `createExecutiveFactory()` 返回 `LanguageExecutiveFactory`。Factory 使用 binding target 和类型化 options 创建 Executive，并检查 runtime options 以及返回 Executive 的 Spec 身份。

### 10.4 定义解释器和插件

```cpp
class LanguageInterpreter : public srt::ContribInterpreter {
public:
    srt::Expected<std::vector<std::unique_ptr<srt::ContribImportValidator>>>
        createImportValidators() const override;

    srt::Expected<std::vector<std::unique_ptr<srt::ContribSpecExtension>>>
        createExtensions(srt::ContribSpec &spec) const override;

    srt::Expected<std::unique_ptr<srt::ContribExports>>
        createExports(const srt::ContribSpec &spec) const override;

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        createConfiguration(const srt::ContribSpec &spec) const override;

    srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
        createImportOptions(const srt::ContribSpec &target,
                            const srt::JsonValue &manifestOptions) const override;

    srt::Expected<std::unique_ptr<srt::ContribImportBinding>>
        createImportBinding(srt::ContribSpec &importer,
                            const srt::ContribImport &declaration,
                            srt::ContribSpec &target,
                            std::unique_ptr<srt::ContribImportOptions> options) const override;
};

class LanguageInterpreterPlugin : public srt::ContribInterpreterPlugin {
public:
    static constexpr const char *IID = "com.example.LanguageInterpreter";

    srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
        create(std::string_view interfaceName, int level,
               std::string_view variant) override;
};
```

插件类使用 `STDC_EXPORT_PLUGIN` 导出。构建系统使用 `stdc_add_plugin_metadata()` 把等于 Category `interpreterIid()` 的 IID 嵌入动态库。`plugin.json` 不写 IID，其根级 `interpreters` 必须声明每个受支持三元组。插件的 `create()` 仍应检查收到的三元组，不要假设 metadata 永远正确。

### 10.5 注册与宿主配置

把 Category 注册代码放进宿主确定会链接和初始化的库中：

```cpp
static srt::ContribCategoryRegistry::Add<LanguageCategory> languageCategoryRegistration(
    "com.example.language", "");
```

宿主在构造 SynthUnit 后配置该 Category 的插件目录：

```cpp
unit.setPluginPaths("com.example.language", {pluginRoot / "languageinterpreters"});
```

此后 Package 可以在 `contributions.com.example.language` 中声明条目，任意 Module 的 `ref` 也可以引用 `:com.example.language/id`。无需修改 SynthRT 的 ModuleReference 语法。

## 11. 扩展 EntryOnly Category

如果 Contribution 只是索引数据，没有公共 Module 声明、解释器、Binding 或 Executive，则使用 EntryOnly：

```cpp
class DictionaryCategory : public srt::ContribCategory {
public:
    DictionaryCategory() : ContribCategory("com.example.dictionary", EntryOnly) {
    }

protected:
    srt::Expected<std::unique_ptr<srt::ContribSpec>>
        createSpec(const srt::ContribCreateContext &context) const override;
};
```

EntryOnly Category 的 interpreter IID 必须为空。`ContribCreateContext::manifestEntry()` 是它的主要输入，`declarationPath()` 和 `manifestDeclaration()` 返回空。Module 公共字段访问器对这种 Spec 没有意义，不得调用。非 Module Contribution 也不能成为 Module import 的 ref 目标。

如果一种 Contribution 需要 Provider、Binding、Executive 或运行时资源，它应建模为 Module Category，而不是在 EntryOnly Category 中私自绕过加载事务。

## 12. 新增既有 Category 的实现变体

为既有 Category 增加变体时不需要新 Category：

1. 沿用现有 `interface` 和 `level` 的 exports、options 与运行时契约。
2. 选择新的 `variant`。
3. 定义该 variant 自己的 configuration 格式和类型化对象。
4. 实现解释器及 Executive。
5. 在插件 metadata 中加入新三元组。

Provider 选择要求三元组精确匹配。若 configuration 内部还需要格式版本，应由 variant 自己定义字段并在 `createConfiguration()` 中检查，不要改变 Runtime Level。

## 13. 错误处理和线程要求

公开的可失败操作使用 `Expected<T>`。错误应保留最贴近失败点的 code，并在跨层传播时使用 context 说明当前 Package、Module 或阶段。不要返回空的成功 `unique_ptr`，Loader 会把它视为非法 Provider。

Load 和 release 由 SynthUnit 串行化，但解释器实例会被缓存并服务多个 Spec。运行期的 Executive、Task、Driver 和 Session 仍可能并发执行，扩展实现必须自行同步共享状态。

管理调用必须同步返回。Acquire 和 Ready 期间不得启动业务线程、异步任务或对事务外可见的 callback。运行时执行只能发生在 Commit 后创建的 Executive 中。

## 14. 常见错误

- 在创建 SynthUnit 后才加载包含静态 Category 注册的库。现有 SynthUnit 不会补建该 Category。
- 调用 DataOnly 后再尝试 `addCategory()` 或 `addRuntimeService()`。
- 把 Driver 当作 Contribution，或把解释器当作 Runtime Service。
- 未检查 category 或三元组便调用 `as<T>()`。
- 保存 `ContribCreateContext`，或在 PackageHandle 释放后继续使用 Spec 裸指针。
- 把父 Executive 返回的子指针交给第二个 owning smart pointer。
- 在 PackageHandle 之前没有销毁根 Executive。
- 在 Binding `activate()` 中执行可能失败、分配或 I/O 的工作。
- 在解释器中保存单一“当前 Spec”状态，忽略解释器可跨 Package 复用。
- 让 runtime options、exports、configuration 或 import options 返回错误的三元组。
- 修改插件搜索路径后期待已 Commit Package 热切换解释器。

## 15. 推荐阅读顺序

1. [ds-spec-2.4.md](ds-spec-2.4.md)：清单格式和规范行为。
2. `synthrt/include/synthrt/Core/SynthUnit.h`：宿主入口。
3. `synthrt/include/synthrt/Core/PackageHandle.h` 与 `ContribSpec.h`：加载结果和声明查询。
4. `ContribCategory.h`、`ContribInterpreter.h`、`ContribImportBinding.h` 与 `ContribExecutive.h`：扩展点。
5. `synthrt/include/synthrt/SVS`：内置 singer 和 inference Category。
6. `dsinfer/include/dsinfer/Api`：Level 1 类型化契约。
7. `dsinfer/plugins/inferenceinterpreters` 与 `dsinfer/plugins/singerproviders/diffsinger`：可运行的解释器、Provider、Binding Factory 和 Executive 示例。
8. `dsinfer/tools/cli/main.cpp`：从 Driver 初始化、Package Load、Singer Pipeline 到各推理阶段执行的完整宿主示例。
