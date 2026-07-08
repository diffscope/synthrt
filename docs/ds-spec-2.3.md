# DiffSinger 数据格式与推理接口规范 2.3

> DiffSinger Data Format and Inference Interface Specification 2.3

此规范为 OpenVPI 为各种 AI 推理工具制定的标准，旨在为各种模型提供通用的组织结构与调用接口，使 AI 模型的分发与调用更为有序、规范。

本规范主要指导以下几种基础设施的开发：
1. 安装器（Installer）
2. 加载器（Loader）
3. 执行器（Executor）

## 1. 关于 Library（亦称 Package）

### 文件结构

本规范内，可分发的数据包的最小单位是 Library（库），是一个以`dspk`为扩展名的 ZIP 格式的压缩包。

压缩包内基本结构为：
```
+ xxx.dspk
  - desc.json
  - ...
```

Library 内多使用`json`作为声明文件，我们规定，声明文件中使用的相对路径的基路径是这个声明文件的在所目录。

#### 描述文件

`desc.json`是 Library 的描述文件，主要包括以下内容。

```json
{
    "$version": "1.0",
    "id": "foo",
    "version": "1.0.0.0",
    "compatVersion": "0.0.0.0",
    "vendor": "someone",
    "copyright": "Copyright (C) someone",
    "description": "Some library",
    "readme": "assets/readme.txt",
    "url": "https://www.example.com",
    "contributes": {
        "inferences": [
            "./inferences/acoustic/config.json",
            "./inferences/duration/config.json"
        ],
        "singers": [
            "./characters/zhibin/manifest.json",
            "./characters/lili/manifest.json"
        ]
    },
    "dependencies": [
        {
            "id": "bar",
            "version": "1.0.0.0"
        }
    ]
}
```
+ 必选字段
    + `id`：唯一标识符，不准出现以下字符`/\[]:;'"`
    + `version`：版本号，格式为`x.y[.z][.w]`，`z`和`w`可独立省略
+ 可选字段
    + `$version`：文件格式版本，当前固定为`1.0`
    + `compatVersion`：兼容到的最低版本，如果为`0.0.0.0`表示向下兼容所有，如果与`version`相同则表示不向下兼容，缺省为不向下兼容
    + `vendor`：提供者，可提供多语言，多语言形式如下（`_`指定首选名称）
        ```json
        {
            "_": "Tadokoro Koji",
            "zh_CN": "李田所",
            "ja_JP": "田所浩二"
        }
        ```
    + `copyright`：版权信息，可为多语言
    + `description`：介绍文字，可为多语言
    + `readme`：放置介绍、许可证等信息的文本
    + `url`：网站
    + `contributes`：功能贡献列表，主要包含子模块
        + `inferences`：推理模块清单（**字符串数组**，每个元素为指向推理配置文件的路径）
        + `singers`：歌手模块清单（**字符串数组**，每个元素为指向歌手清单文件的路径）
    + `dependencies`：依赖的库
        + `id`：依赖库 ID
        + `version`：依赖库版本
        + `required`：是否为强制依赖，默认为`true`

#### 依赖项

当前 AI 推理现状是，一个模型动辄超过 100MB 甚至 1GB，因此内存与显存是宝贵的，本规范使用了一种常见的方式缓解这个问题。

为了模块复用与增量更新，`dspk`引入了依赖机制。

- 模块复用：开发者要分发若干个`dspk`（假设为 A、B），但是它们之间存在一些可以复用的内容，为了节省存储时的硬盘资源以及推理时的内存资源，那么可以将这些内容独立到一个`dspk`（C）中，在 A 与 B 的描述文件中声明它们依赖 C 即可。

- 增量更新：开发者要分发一个`dspk`（A），A 中存在稳定与不稳定的部分，不稳定的部分每次更新都需要更改，为了节省网络资源，那么可以将不稳定的内容独立到一个`dspk`（B）中，每次更新时只需更新 B 即可。

### 安装与加载

#### 安装

对于`dspk`文件，安装就是在某个目录中解压它。本规范对这个目录没有任何要求。

#### 加载

由于`dspk`引入了依赖机制，因此一个`dspk`的加载流程中包含依赖查找。

- 对于一个`dspk`，当且仅当它的所有`required`为`true`的依赖项都能被加载，它才能被加载。
- `required`为`false`的依赖项为可选依赖；若找不到则跳过，不影响加载结果。
- 一个`dspk`在被成功加载之前，不进行后续对其内部其他文件的任何访问。
- 一个`dspk`的依赖项给定了所需依赖项的`id`与`version`，只有满足以下条件才能被视为合法依赖。
    - `id`与给定`id`一致
    - `version`大于等于给定`version`
    - `compatVersion`小于等于给定`version`
- 一个`dspk`将在其`required`依赖项全部加载后进入初始化，初始化每一步都成功后即为成功加载。

#### 简单方案

本节将给出一种简单的安装加载方案。

安装器将所有`dspk`安装到同一个目录（如`~/.diffsinger/lib`），所有被安装的`dspk`平铺在这个目录中。
```
+ ~/.diffsinger
  + lib
      + lib1
        - desc.json
        - ...
      + lib2
        - desc.json
        - ...
  - ...
```

加载器可由用户在启动时指定一个路径列表（如`~/.diffsinger/lib;~/lib1;~/lib2;~/lib3`），当加载器加载某个`dspk`并伴随着解析其依赖时，加载器将依次遍历这个列表，尝试每个路径，如果在这个路径中找到了符合条件的依赖则加载之，并按同样的方法解析下一个依赖，直到结束。

## 2. 模块

### Inference 模块

Inference 模块负责执行某一项参数的推理任务，承担了最底层、核心的工作。

#### 配置文件

```json
{
    "$version": "1.0",
    "name": "Zhibin - Variance",
    "level": 1,
    "schema": {
        "predictions": [
            "breathiness", "duration"
        ]
    },
    "configuration": {
        "hiddenSize": 512
    }
}
```
+ 必选字段
    + `level`: 推理解释器应选择的 API 版本
+ 可选字段
    + `$version`：文件格式版本，当前固定为`1.0`
    + `name`: 推理模块名称，可为多语言，如为空则与`id`一致
    + `schema`: 输出参数的限制条件
    + `configuration`：配置信息

### Singer 模块

Singer 模块负责定义一个或若干个歌手的信息，以及其需要使用的推理库。

#### 声明文件

`singer.json`是 Singer 的信息声明文件，主要包括以下内容。

```json
{
    "$version": "1.0",
    "name": "Zhibin",
    "level": 1,
    "class": "diffsinger",
    "avatar": "../assets/avatar.png",
    "background": "../assets/sprite.png",
    "demoAudio": "../assets/demo.wav",
    "imports": [
        {
            "id": "acoustic-1",
            "options": {}
        },
        {
            "id": "bar/pitch",
            "options": {
                "roles": [
                    "pitch"
                ]
            }
        },
        {
            "id": "variance-A",
            "options": {
                "roles": [
                    "tension",
                    "energy"
                ]
            }
        }
    ],
    "configuration": {
        "defaultLanguage": "cmn",
        "speakers": [
            {
                "id": "ice",
                "name": "ice",
                "toneRanges": { "min": "C1", "max": "B7" }
            }
        ],
        "languages": [
            {
                "id": "cmn",
                "name": { "_": "Mandarin", "zh_CN": "普通话" },
                "g2p": "g2p-cmn-official",
                "dict": "../assets/opencpop-extension.txt",
                "onsetFile": "../assets/opencpop-extension_onset.json",
                "onsetMode": "rule",
                "s2pMode": "dict"
            },
            {
                "id": "eng",
                "name": { "_": "English", "zh_CN": "英语" },
                "g2p": "g2p-eng-official",
                "onsetFile": "../assets/eng.json",
                "onsetMode": "rule",
                "s2pMode": "direct",
                "dict": "../assets/dictionary-en.txt"
            }
        ]
    }
}
```
+ 必选字段
    + `$version`：文件格式版本，当前固定为`1.0`
    + `level`: 歌手所属的 API 版本
+ 可选字段
    + `name`: 歌手名称，可为多语言，如为空则与`id`一致
    + `class`: 歌手架构（如`diffsinger`），用于路由到对应的 SingerProvider
    + `imports`：歌手依赖的推理模块
        + `id`：推理模块 ID。跨库引用时使用`<libId>[/<libVersion>]/<moduleId>`的形式，如`bar/pitch`表示引用 bar 库中的 pitch 推理模块。`lib`和`version`可省略，省略时表示在同库内查找
        + `options`：提供给引用的推理模块的选项，需要符合对应的 API 版本以及推理模块的`schema`的限制
    + `avatar`：头像
    + `background`：可用于 SVS 编辑器显示的立绘背景
    + `demoAudio`：可用于 SVS 编辑器预览的声音
    + `configuration`：业务配置，由 SingerProvider 解析
        + `defaultLanguage`：默认语言 ID（如`"cmn"`、`"eng"`）
        + `speakers`：说话人列表数组
            + `id`：说话人 ID
            + `name`：说话人显示名称
            + `toneRanges`：音域范围，使用科学音高记号（SPN），如`C4`表示中央C
        + `languages`：语言配置列表数组
            + `id`：语言 ID（ISO 639-3 格式，如`"cmn"`、`"eng"`）
            + `name`：语言显示名称
            + `g2p`：G2P 标识符（如`"g2p-cmn-official"`），用于路由到具体 G2P 实现
            + `dict`：该语言对应的字典路径
            + `onsetFile`：起声配置文件路径
            + `onsetMode`：起声检测模式（`"rule"`等）
            + `s2pMode`：分段模式（`"dict"`（从 `dict` 读取）、`"direct"`等）
            + `g2pPackages`：自定义 G2P 包路径（支持 string 或 string 数组）。非空时启用声库私有 G2P 上下文，缺失或为空时使用官方 G2P
            + `g2pPackageVersion`：自定义 G2P 包的版本。缺失时回退到声库 packageVersion

#### 注意事项

- 每个歌手的预设所用到的`id`都必须在`desc.json`的`dependencies`中声明。

#### 关于 `g2pPackages` 与 G2P 上下文

`configuration.languages[].g2pPackages` 字段决定 G2P 路由的上下文归属：

| `g2pPackages` 状态 | G2P 上下文 | context 值 | g2pSource |
|---------------------|-----------|-----------|-----------|
| 缺失或为空 | 官方 G2P（默认上下文） | `""`（空字符串） | `"official"` |
| 非空（一个或多个路径） | 声库私有 G2P | `singerId` | `"voicebank"` |

- `g2pPackageVersion` 缺失时，回退到声库的 `version` 字段
- 同一声库的不同语言可声明不同的自定义 G2P 包
- 框架层采用精确 `ContextKey(context, version)` 匹配，无版本回退

#### 关于 API Level

由于 DiffSinger 引擎架构的复杂性，我们为推理模块引入了 API Level 的概念，在声明文件中用`level`表示。

`$version` 与 `level` 的区别：
- `$version` —— 文件格式版本，用于 JSON schema 兼容性判断，当前固定为`"1.0"`
- `level` —— API 接口版本（正整数），用于模型接口的版本管理，随模型输入/输出格式变更递增

API Level 代表引擎接口的版本号，是一个正整数，每当模型的输入或输出格式发生更新时，它将向上递增。引擎官方为每个 API Level 制定一套描述模型功能的语法。

每个 Inference 模块在声明文件，使用`level`声明自己所属的 API Level，在`schema`字段中按照该 Level 规定的语法公开自己所支持的功能集合，在`configuration`字段中按照语法填写模型相关参数。

每个 SingerModule 模块在声明文件，使用`level`声明自己所属的 API Level，在`configuration`字段中填写歌手相关参数。

每个 Singer 模块在其`imports`字段中导入其依赖的 Inference 模块，在每个导入项的`options`字段中填写其选择的一部分功能，`options`的值需要符合其依赖的 Inference 模块所属的 API Level 语法。

### 可扩展性

- 具有新功能的模型开发完成后，开发者为之起一个`class`名，再基于现有的推理程序开发一个与这种模型匹配的解释器，这样即可扩展推理功能。

- 非 DiffSinger 甚至非 AI 的开发者，如 UTAU、Vocaloid，亦可通过扩展`class`来支持其他引擎，可以使用混合 Library 将歌手信息与歌声采样放在同一个 Library 中。

### 推荐目录结构

```
+ somedspk
  + assets
    - avatar.png
    - dict.json
  + inferences
    + acoustic
      - config.json
      - acoustic.onnx
    + duration
      - config.json
      - duration.onnx
    - ...
  + singers
    + singer1
      - manifest.json
  - desc.json
```

- 共享的资源文件放置在`assets`中
- 推理模块放置在`inferences`的子目录中，每个子目录配置一个`config.json`
- 歌手模块放置在`singers`的子目录中，每个子目录配置一个`manifest.json`
- 根目录固定放置`desc.json`

#### 自定义 G2P 包推荐目录结构

当声库携带自定义 G2P 时，推荐如下组织方式：

```
+ somedspk
  + g2p
    + custom-g2p-cmn
      - package.json        # G2P 包描述
      - model.onnx          # G2P 模型
      - config.yaml         # 配置
    + custom-g2p-yue
      - ...
  + assets
    - ...
  + singers
    - ...
  - desc.json
```

`configuration.languages[].g2pPackages` 中的路径指向 `g2p/` 目录下的各个 G2P 包子目录。

## 3. 工具开发

synthrt 库提供以下命令行工具：

- `dsinfer-cli`：推理 CLI（由 dsinfer 库提供）
- `dspk-pack-cli`：声库包打包/校验/解包工具（由 synthrt-voicebank 提供）

### dsinfer-cli 功能

- 校验
- 显示安装的包
- 安装
- 卸载
- 自动卸载
- 命令行推理

### dspk-pack-cli 功能

- `dspk-pack-cli validate <path>`：校验声库包完整性（检查 manifest/JSON/文件存在/hash）
- `dspk-pack-cli pack <src> <out.dspk>`：将目录打包为 dspk 文件
- `dspk-pack-cli unpack <in.dspk> <dir>`：解包 dspk 文件到目录
- `dspk-pack-cli info <path>`：查看声库包元数据

#### dsinfer-cli 默认配置文件

`dsinfer-cli`会在以下路径搜索`dsinfer-conf.json`，此文件为其指定默认安装路径与默认推理驱动极其初始化参数。
- `<可执行文件目录>`
- `<用户目录>/.diffinger`

`dsinfer-conf.json`文件内容如下：
```json
{
    "paths": [
        "/home/user/.diffinger/packages"
    ],
    "driver": {
        "id": "onnx",
        "init": {
            "ep": "dml"
        }
    }
}
```

#### 安装状态文件

在一个目录中安装了包后，`dsinfer-cli`会留下一个记忆文件`status.json`。
```json
{
    "packages": [
        {
            "id": "zhibin[5.1]",
            "path": "zhibin-5.1",
            "contributes": [
                "singers",
                "inferences"
            ]
        }
    ]
}
```

### Package 发布

使用`dspk-pack-cli pack`命令来将整理好的目录打包为`dspk`，此命令将执行校验与压缩功能。

打包完成的`dspk`中，其根目录会加入`package-info`目录，目录内有`manifest.json`，存储文件校验信息。存在此目录及其文件，且内容合法的`dspk`才能被安装。

```json
{
    "$version": "1.0",
    "files": [
        {
            "path": "desc.json",
            "crc32": "xxx"
        }
    ]
}
```

### 推理插件开发

在`plugins`中添加插件。

#### 推理解释器

创建派生于`InferenceInterpreter`的解释器类。

- `apiLevel`：返回解释器支持的最高 api 等级
- `key`：返回对应的推理参数类型的`class`，如`com.diffsinger.InferenceInterpreter.PitchInference`
- `validate`：校验推理模块是否符合规范，以及使用某个推理模块的歌手模块是否指定了正确的参数
- `create`：创建对应的推理任务类

#### 推理任务

创建派生于`Inference`的推理任务类。

- `initialize`：初始化推理任务，应当加载需要用到的模型
- `start`：开始推理任务，应当对输入的参数进行预处理，并构建推理图
- `startAsync`：`start`的异步版本
- `stop`：立即停止推理任务（同步）
- `state`：推理任务状态
- `result`：推理结果
