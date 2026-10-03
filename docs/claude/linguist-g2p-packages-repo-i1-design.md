# wolf 私有 port 机制与 G2p 资源仓 spec 2.4 化设计（I1 实施轮 · 第二轮定稿）

> **状态**：本文是 [linguist-g2p-package-distribution-draft.md](linguist-g2p-package-distribution-draft.md)
> （round-2，D-P1~P8）的 **I1 实施轮设计**（**2026-10-03 复查：该草稿不在 synthrt / wolf / otter 任一仓中，上面的链接已失效**；本文对它的引用按历史记录读）。第一轮（2026-09-06）设计了资源仓/port/测试
> 模块骨架并以"外部安装挂载"解决 wolf × synthrt main；**第二轮（2026-09-07）按用户指令
> 与四项拍板整体修订**：wolf 建立自有私有 port 机制、synthrt(main) 改经 port 消费、
> 资源仓首发即 spec 2.4 化（格式迁移自 I2 提前，资源文件与正则不变）、lite 本轮零改动。
> 冲突时以发行设计文档与 spec 2.4 为准；实施完成后 D-W1~W9 回写上位文档台账。
>
> **第三轮修订（2026-09-07）**：资源仓**落地为本地项目** `D:\projects\wolf-g2p-packages`
> （git main，初始提交 `613035a`，骨架=README/LICENSE/.gitignore/packages 契约/
> `scripts\make-release.ps1` 可用实现，未接远端、未推送）；新增 D-W8（wolf CMake
> 选项与私有层 `wolf` port）、D-W9（测试宿主模拟层 HostSim）与 §6 逐链路打通表；
> §2.7 脚本契约更新为实现态（REVIEWED 审定门控、资产命名规则、装配约定）。
>
> **锚点口径**（2026-09-07 实测）：synthrt main `a060af0`（新框架：
> `SynthUnit::addCategory/setPackagePaths/openPackage(OpenMode{DataOnly,Load})`，
> `synthrt/include/synthrt/Core/SynthUnit.h:48/62/77`）；synthrt refactor 线 `814bf81`
> （overlay 共享 port 现钉，lite 消费）；两线分叉于 `304b275`；wolf `cecedba`；
> overlay 子模块 lite pin `f2aff64`、wolf pin `04b3297`。套件形态实测：13 套
> `g2p.chain.ChainG2pInference`（含 Eng）、Cmn `g2p.template.MandarinG2pInference`、
> Yue `g2p.template.CantoneseG2pInference`、Multi `g2p.model.Multig2pInference`。

## 决策台账

**第一轮（2026-09-06）**：

- **D-W2（测试数据双平面）**→ 第二轮修订见 D-W2'；
- **D-W3（数据缺席语义）**：配置期门控为主（CMake 知道数据在不在，缺席则相关测试
  目标整体不注册），测试体内早退兜底逐套件缺口；不做"注册了再假通过"。**维持不变**；

**第二轮（2026-09-07，选项式提问拍板）**：

- **D-W1'（synthrt main 供给通道，修订第一轮 D-W1）**：**经 wolf 私有 overlay 的
  `synthrt` port**（同名遮蔽共享 overlay 的 refactor pin，REF 换 main `a060af0`），
  manifest 直连依赖；废止"外部安装挂载"与 wolf README 既有构建说明段（同步改写）；
  硬红线：**wolf 使用独立 vcpkg install root**——lite 共享实例（`ds-editor-lite/vcpkg`）
  里已装 refactor 版 synthrt，同名包二进制会互相覆盖；
- **D-W4（私有 port 机制落点）**：**wolf 仓内直属目录 `scripts/vcpkg-local/ports/`**
  （wolf 自有 git 文件，非子模块），manifest 双层 overlay 私有在前遮蔽
  （vcpkg 多 overlay 同名 port 列表靠前者胜、overlay 压过内建 registry，Microsoft Learn
  《Overlay ports》）；共享子模块继续供 qmsetup/stdcorelib/stdcorelib-plugin/boost-test，
  **零改动**；机制文档落 `wolf/AGENTS.md`（镜像 lite AGENTS.md 口径）；
- **D-W5（资源仓首发即 spec 2.4 化）**：新建资源仓，包元数据按 spec 2.4 + linguist
  level-1 重写（`desc.json`/语言声明/inference 声明），**资源文件、词典、正则原样平迁**；
  格式迁移自上位文档 I2 提前到首发——资产自首发起即可被 main 框架 `openPackage(Load)`
  加载，D-W2 双平面的"过渡期格式断层"不复存在；
- **D-W6（Num/Punc/Unknown 辅助套件）**：**照转独立 G2P 包**——只含 `inference` 贡献、
  无 `linguist` 条目，资源平迁；manifest/README 标注「旧栈语言服务链辅助片段、新栈
  消费面未定义」；
- **D-W7（首发范围与 exports 面作者归属）**：**15 套全转首发**（Eng 维持排除）；
  `exports.phonemes/symbols/languages` 与逐套件语言贡献 ID 由脚本按变体文档推导口径
  生成候选，**用户逐套审定后入库**（变体文档既判：夹具零痕迹、导出面纯目标）；
- **D-W8（wolf CMake 选项与私有层 `wolf` port，第三轮）**：测试编译的单一开关 =
  既有 `WOLF_BUILD_TESTS`（缺省 OFF）；**port 安装树恒不含测试**（portfile 无条件
  `-DWOLF_BUILD_TESTS:BOOL=OFF`，不设 tests feature——安装树携带测试可执行文件属
  反模式）；开发/CI 的"灵活"体现在两侧分工：vcpkg manifest `--x-feature=tests,g2p-tests`
  只管**依赖可得性**（boost-test + 包数据），CMake `-DWOLF_BUILD_TESTS=ON` 才编译
  测试——数据/依赖在而开关关时零编译负担，开关开而依赖缺时 `AddAutoTest` 对
  Boost 缺席"扩为空"优雅降级（main 先例）。`wolf` port 落私有层（第三个 port）；
- **D-W9（测试宿主模拟层 HostSim，第三轮）**：wolf 的 E2E 测试经统一宿主模拟层
  `src/tests/support/HostSim` 执行，调用序列**镜像 lite 宿主的集成模式**
  （lite `SynthrtEngine::initialize` 的 main 世界等价物，见 §4.1），保证测试验证的
  就是未来宿主真实的调用路径。

---

## 1. wolf 私有 vcpkg port 机制（D-W1'/D-W4）

### 1.1 目录与接线

```
wolf/
  AGENTS.md                        // 新建：机制文档（§1.4）
  scripts/
    vcpkg/                         // 既有子模块（stdware/vcpkg-overlay），零改动
    vcpkg-local/                   // 新建：wolf 私有 overlay（wolf 直属文件）
      ports/
        synthrt/                   // 私有 port：pin main（§1.2）
        wolf/                      // 私有 port：wolf 库自身（§1.6，D-W8）
        wolf-g2p-packages/         // 资源包 port（§3）
    vcpkg-manifest/
      vcpkg.json                   // 既有 manifest，改 overlay-ports + 增依赖（§1.3）
```

- manifest `vcpkg-configuration.overlay-ports` 改为
  `["../vcpkg-local/ports", "../vcpkg/ports"]`——**私有层在前**：同名 `synthrt` port
  由私有定义遮蔽共享定义（refactor pin 继续服务 lite），其余 port 落到共享层解析；
- 共享子模块两仓（lite/wolf）各自 pin 各自提交的工作方式不变（lite `f2aff64` /
  wolf `04b3297`），本轮 wolf 的子模块指针**无需 bump**（私有层不进共享仓）。

### 1.2 私有 port：synthrt（pin main）

- portfile 仿共享 overlay 现行 `ports/synthrt/portfile.cmake`（策略头部、ORT 经
  `onnxruntime-builds` 依赖、config fixup 保父目录手法全数沿用），仅换 pin：
  `REF a060af0…`（实施时取 main 实时 HEAD 或钉本轮锚点，SHA512 由 vcpkg 取包实测回填）、
  `HEAD_REF main`；
- 配置选项沿用 `-DSYNTHRT_BUILD_TESTS:BOOL=OFF`；`SYNTHRT_BUILD_DSINFER` 保持默认 ON
  （main 顶层 `CMakeLists.txt:12-14` 三选项实测在位）；
- main 的 config 包供出 `SYNTHRT_MODULES_DIR`（`synthrt/synthrtConfig.cmake.in:5`），
  wolf 测试基建（`AddAutoTest.cmake`）随之就位；
- **实施核对项**：main 树 root CMakeLists 同时收编 `dsinfer/` 子目录，config fixup 需
  覆盖 synthrt 与 dsinfer 两套 config（现行 portfile 的保父目录 fixup 写法是否直接
  适用，实施时以干净安装树核验）。

### 1.3 wolf manifest（`scripts/vcpkg-manifest/vcpkg.json`）

```json
"dependencies": [ "qmsetup", "stdcorelib", "stdcorelib-plugin", "synthrt" ],
"features": {
    "tests":   { "description": "Enable test cases support",
                 "dependencies": [ "boost-test" ] },
    "g2p-tests": { "description": "Fetch G2p language packages for linguist tests",
                   "dependencies": [
                       { "name": "wolf-g2p-packages", "features": ["cmn", "multi"] }
                   ] }
}
```

- `synthrt` 首次进入依赖列表（D-W1'）；私有层遮蔽保证其来自 main pin；
- 默认测试面取 `cmn`（algo-pinyin 主链）+ `multi`（pipe-chain × multig2p-onnx 模型链），
  覆盖两类宿主面变体；其余套件按测试需要增删 feature 清单，port 侧零改动（D-P6 口径）。

### 1.4 wolf AGENTS.md（新建，镜像 lite 口径）

内容骨架：项目定位（synthrt 的语言层库）→ 构建系统（vcpkg manifest 位于
`scripts/vcpkg-manifest/vcpkg.json`、**双层 overlay**：`scripts/vcpkg-local/ports` 私有
层在前遮蔽共享子模块层、工具链文件、install 命令行
`vcpkg install --x-manifest-root=../scripts/vcpkg-manifest --x-install-root=./installed
--triplet=x64-windows`、feature 开关 `--x-feature=tests` / `--x-feature=g2p-tests`）→
**红线**（install root 必须 wolf 自有，不得指向 lite 共享实例）→ VS/Qt 之外的环境前置
（CLion 捆绑 cmake ≥ 4.x + vcvars，同 synthrt-build 教训）→ Gotchas（同名 port 遮蔽
方向、私有层与共享层的分工表）。

### 1.5 README 构建段改写与 CI

- wolf README「Setup Environment」段的 *"synthrt is not a vcpkg dependency here"* 一节
  **废止改写**：synthrt 自本轮起经私有 port 依赖，删除 CMAKE_PREFIX_PATH 外挂说明；
- CI：纯 vcpkg 流水线（manifest install 即得 main 版 synthrt），不再需要 synthrt 构建
  job；install root 缓存即可。

### 1.6 `wolf` port（私有层第三 port，D-W8）

- **vcpkg.json**：`name: wolf`、`version-string` 跟 wolf `PROJECT_VERSION`
  （0.0.1.0）；依赖 `synthrt`（私有层 main pin）/`stdcorelib`/`stdcorelib-plugin`；
  **不设 tests feature**——安装树不含测试（D-W8）；
- **portfile**：`-DWOLF_BUILD_TESTS:BOOL=OFF`（无条件）、`-DWOLF_INSTALL:BOOL=ON`；
  `vcpkg_cmake_config_fixup(PACKAGE_NAME wolf)`；产物 = `wolf::wolf` 库 +
  `wolflinguistprovider` 插件（后者是宿主运行所需交付物，随库安装）；
- **消费定位**：本轮供 CI 自证（port 流水线全绿 = 默认关测试的安装面成立）与未来
  宿主直装；lite 迁 main 时评估上提共享 overlay（同 wolf-midi 先例并列）；
- **开发/CI 测试构建不经 port**：wolf 仓 manifest 构建 + `--x-feature=tests,g2p-tests`
  + `-DWOLF_BUILD_TESTS=ON`（分工表见 D-W8）。

## 2. 资源仓 `wolf-g2p-packages`（D-W5/D-W6/D-W7）

### 2.1 仓库定位与 git 极简（已落地）

- **仓已落地（2026-09-07 状态）**：`D:\projects\wolf-g2p-packages`（git main，初始提交 `613035a`，
  骨架=README/LICENSE(Apache-2.0 同 wolf)/.gitignore/packages 契约/
  `scripts\make-release.ps1`（已实现、语法校验通过，待元数据入库后 dry-run））；
  **未接远端、未推送**——GitHub 建仓与 remote 配置为用户动作。
  （**2026-10-03 复查：该目录在本机已不存在**，本节按当日快照读。）
- tag `v<bundleVersion>` 四段式，一次 release = 全部套件快照；zip 资产走 GitHub
  Release、不经 git 对象库；
- **git 内容**：README（仓定位/release 流程/Eng 许可注记）、LICENSE、
  `packages/`（15 套手写 spec 2.4 元数据，KB 级 JSON——这是本轮与第一轮设计的差异：
  元数据成为仓内受审内容）、`scripts/make-release.ps1`、`manifest.json`（生成物快照）；
- **资源文件不入仓**：词典/模型/正则等大体量内容以 synthrt 夹具
  （`D:\projects\synthrt\resources\G2pPackages`）为源，release 脚本按参数装配——
  git 历史恒小，夹具单源不重复（D-P3 口径延续，夹具退役时点后移到资源仓自持内容为止）。
  （**2026-10-03 复查：该夹具目录现在只剩一份说明 `README.md`，套件本身已不在 synthrt 树内**；
  本节与 §3 的 `-FixtureRoot`、`:271` 的「夹具缺 `Phonetic-Suite-<Suite>` 即配对失败」门控都按当日快照读，
  实际装配前需先把夹具放回该目录或另指来源。）

### 2.2 仓库结构（实现态）

```
wolf-g2p-packages/                # 本地 D:\projects\wolf-g2p-packages（git main, 613035a）
  README.md  LICENSE  .gitignore
  scripts/make-release.ps1        # 发布脚本（实现态契约见 §2.7）
  packages/
    README.md                     # 元数据契约：目录内容、资源装配约定、审定工作流
    Cmn/ ...                      # 逐套件元数据（待推导生成 + 审定，§2.6）
      desc.json
      linguists/<贡献id>/linguist.json | g2p/inference.json | s2p/inference.json
      linguists/<贡献id>/phonemes.json（或内联）
      REVIEWED                    # 审定标记：缺席即不进 release（D-W7 的机检载体）
  manifest.json                   # 生成物快照（随每次 release 提交）
```

### 2.3 desc.json 规范（spec 2.4 契约面）

```json
{
    "$version": "1.0",
    "id": "wolf/lang-cmn",
    "version": "1.0.1.0",
    "compatVersion": "1.0.1.0",
    "runtimeLevel": 1,
    "vendor": "wolf",
    "copyright": "Copyright (C) wolf",
    "contributions": {
        "linguist":  [ { "id": "cmn-pinyin", "path": "./linguists/cmn-pinyin/linguist.json" } ],
        "inference": [ { "id": "g2p", "path": "./linguists/cmn-pinyin/g2p/inference.json" },
                       { "id": "s2p", "path": "./linguists/cmn-pinyin/s2p/inference.json" } ]
    }
}
```

- `version` 由旧 `package.json` 补四段（Cmn `1.0.1` → `1.0.1.0`、Jpn `0.0.1` →
  `0.0.1.0`）；`compatVersion = version` 起步（spec 缺省单点；后续按上位文档 §3.2
  纪律：非破坏更新只抬 `version`）；`vendor`/`copyright` 承旧值；
- 包 id 定 `wolf/lang-<iso>`（发行文档 §3 示例口径）；Num/Punc/Unknown 同法
  `wolf/g2p-num` / `wolf/g2p-punc` / `wolf/g2p-unknown`（非语言，无 `linguist` 键）；
- 语言贡献 id 按 level-1《ID 形态约定》`<iso-639-3>-<注音体系>`：文档既有
  `cmn-pinyin`/`yue-jyutping`/`jpn-romaji` 可直用，其余套件拟名随 exports 审定
  一并交付（**审定清单**：套件 → 贡献 id → 注音体系命名依据）；
- `vars` 可用 `${root}`/`${dir}` 锚定资源相对布局（spec 2.4:94-95），转换时把
  旧 config 里的 `../../assets/...` 类相对路径改写为相对新声明文件目录——**所指
  文件与正则内容不变，只动锚定基点**。

### 2.4 linguist.json（WolfLinguist Level 1）

```json
{
    "name": { "_": "cmn-pinyin" },
    "interface": "org.openvpi.wolf.linguist.WolfLinguist",
    "level": 1,
    "variant": "wolf",
    "exports": { "phonemes": "./phonemes.json" },
    "configuration": {},
    "imports": [
        { "role": "linguist/g2p", "ref": ":inference/g2p" },
        { "role": "linguist/s2p", "ref": ":inference/s2p" }
    ]
}
```

- `configuration` **显式空对象**（现行实现下省略即 Null 被拒，level-1 实现注记）；
- imports 固定基数 g2p+s2p（onset 无资源，缺省）；语言贡献 id 需命中 G2P 模块
  `exports.languages`（level-1 Ready 校验），故 g2p 声明的 `languages` 与本文件
  的贡献 id 必须一致——审定清单的联动项。

### 2.5 逐套件转换映射（资源与正则不变，元数据重写）

| 旧套件（实测 class） | 新形态 | 资源处置 |
| :-- | :-- | :-- |
| Deu/Fra/Ita/Jpn/Kor/Por/Rus/Spa/Fil 等 12 个 chain 套件 | G2P `pipe-chain`（变体文档 §3；`chain.json` 步骤/正则/词典原样，`formatVersion` 公约照 §1.2/§3） | 词典 txt 原样；config 内 regex 原样 |
| Cmn（MandarinG2pInference） | G2P `algo-pinyin`，`scheme:"mandarin"`（§4 键汇：scheme/dictPath/verify） | `dict/mandarin/` 原样；verify 的 dict/regex 条目原样 |
| Yue（CantoneseG2pInference） | G2P `algo-pinyin`，`scheme:"cantonese"` | 同上 |
| Multi（Multig2pInference） | G2P `pipe-chain` 前端 + `G2PModel multig2p-onnx` 后端（§5；G2PModel 为 backend-only、必须经 pipe-chain imports 消费） | `bundle.json`/onnx/`vocabulary.json` 原样；训练段等非运行时键不迁（按 §5 运行时键汇裁剪，裁剪清单随审定交付） |
| s2p | cmn/yue = `dict`变体（**实测证据**：`ds-zh-pinyin-lite.txt` 即「拼音⇥音素序列」TSV）；chain 族 = `direct`（链尾输出即空格分隔音素，§6 注 1 语义）——逐套件按 §6 口径复核后定 | 无新资源（direct）；dict 变体引用既有 TSV |

### 2.6 exports 面推导与审定流程（D-W7）

1. `make-release.ps1 -DeriveExports` 按推导口径生成候选：S2P `dict` 取 TSV 目标列
   并集、`direct` 取 G2P `exports.symbols`（chain 族 = 词典值 token 并集，D22 口径）；
   `exports.languages` = 本套件贡献 id 单元素；`exports.symbols` 同 phonemes 口径；
2. 候选落 `packages/<套件>/` 工作副本 + 生成《审定清单》（套件 × 贡献 id × phonemes
   计数 × 抽样样本），**用户逐套审定**；修订以提交进入仓；
3. 审定通过前不出 release——发布物只含审定完成的套件（首发范围可按审定进度分批
   tag，机制不变）。

### 2.7 make-release.ps1 契约（实现态）

```
.\scripts\make-release.ps1 -BundleVersion 1.0.0.0 `
    [-FixtureRoot D:\projects\synthrt\resources\G2pPackages] `
    [-OutDir <repo>\out\release-<bundleVersion>] [-IncludeUnreviewed]
```

- **枚举与门控**：只收 `packages/<Suite>/` 含 `desc.json` **且带 `REVIEWED` 标记**的
  套件（D-W7 的机检载体；`-IncludeUnreviewed` 供 dry-run）。Eng 的排除不需要参数——
  它没有元数据目录，天然不进枚举；配对失败（夹具缺 `Phonetic-Suite-<Suite>`）即
  报错终止；
- **装配约定**（`packages/README.md` 同文）：包根 = 仓内元数据 ∪ 夹具套件内容
  **字节原样平迁**，仅剔除夹具 `package.json`（由 `desc.json` 取代）与仓侧
  `REVIEWED` 标记；资源引用路径由元数据按"并入后布局"书写（= 夹具内原相对路径），
  正则/词典值不改写、只改锚定基点；
- **校验**：`desc.json` 必选字段（`$version=="1.0"`/id/version 四段/`runtimeLevel==1`/
  contributions）+ id 形态（语言包 `wolf/lang-<iso>`、辅助包 `wolf/g2p-<name>`）；
- **压缩**：.NET `ZipArchive` 手工逐条目（**正斜杠条目名**，规避
  `Compress-Archive`/PS5.1 反斜杠坑），包根为 zip 顶层；
- **产物**（全落 `-OutDir`，不改仓库工作区）：`wolf-lang-<iso>-<v>.zip` /
  `wolf-g2p-<name>-<v>.zip` × N、`manifest.json`（bundleVersion + packages[]：
  id/file/sha512/version/compatVersion/`format:"spec24-dir"`/breaking/linguist 标记）、
  `assets.cmake`（套件键=目录名小写，§3.2 格式）、`release-notes.md` 草稿；
- **后续人工段**：审计产物 → 提交 manifest 快照 → tag → GitHub Release →
  assets.cmake 同步 wolf 私有 port（无跨仓子模块 bump，比第一轮更简）。

## 3. port：wolf-g2p-packages（wolf 私有 overlay 内）

### 3.1 五件套（仿 ffmpeg-builds 四件套 + assets 生成物）

- **vcpkg.json**：`version-string` = bundleVersion；features 逐套件
  （`cmn/yue/jpn/deu/…/multi/num/punc/unknown`），default-features = 全量缺 `eng`
  （`eng` feature 保留位，请求即 FATAL_ERROR 提示许可未核实）；`supports` 全平台；
- **portfile.cmake**：include assets → 逐套件 `vcpkg_download_distfile`
  （URL = `https://github.com/diffscope/wolf-g2p-packages/releases/download/v${BUNDLE}/<file>`）
  → 校验解包至 `${CURRENT_PACKAGES_DIR}/share/wolf/packages/<套件>/`（**新布局**：
  spec 2.4 平铺包根，脱离旧 `share/synthrt/G2pPackages` 语境）→ config 装配 fixup；
  纯数据 port 策略同第一轮 §2.2；
- **assets.cmake**（生成物，格式对齐 ffmpeg-builds update-assets 产物）：
  `WOLF_G2P_BUNDLE_VERSION` + `WOLF_G2P_SUITES` + 逐套件 `_FILE/_SHA512`；
- **wolf-g2p-packages-config.cmake.in**：两级 `get_filename_component` 上溯前缀
  （ffmpeg-builds 同款），供出 `WOLF_G2P_PACKAGES_DIR`（= `share/wolf/packages`）、
  `WOLF_G2P_PACKAGES_SUITES`、`WOLF_G2P_PACKAGE_<suite>_DIR`；
- **usage**：`find_package(wolf-g2p-packages CONFIG)` 用法与变量说明。

### 3.2 port 与资源仓的同步纪律

`assets.cmake` 与 `version-string` 为**生成物**，随每次 release 同步并提交（D-P8
流程延续）；同步动作从「提交共享 overlay」改为「提交 wolf 私有层」（D-W4），提交/
推送 wolf 仓即可，无跨仓子模块 bump——比第一轮设计更简。

## 4. wolf 消费与测试模块（D-W2' 修订：单平面真数据）

**D-W2'**：资源首发即 spec 2.4，**测试数据不再分双平面**——

- **真实数据平面（主力）**：`g2p-tests` feature 装得 port 数据（spec 2.4 包），
  测试经 `WOLF_G2P_PACKAGES_DIR` → `SynthUnit::setPackagePaths` +
  `openPackage(path, OpenMode::Load)` 走**真实语言包 E2E**：Provider discovery →
  Package Load 事务 → Executive 创建与父子销毁（`docs/Status.md` 接下来第 4 条）；
  覆盖三类变体链（algo-pinyin / pipe-chain / pipe-chain × multig2p-onnx）；
- **仓内夹具（辅）**：负例与错误语义仍用 wolf 仓内合成夹具（沿 main
  `test_SynthUnit.cpp:363/441` 的运行期合成 `desc.json` 先例 + `AddAutoTest`
  RESOURCES 机制），覆盖 L1 声明解析错误面、依赖区间反例、未知开放 role 等——
  正例走真数据、反例走夹具，各得其所；
- **测试矩阵**：第一轮 L1-L5 表保留，L5 从「过渡期冒烟」升级为真数据 E2E；
  L4 依赖的 provider（G2P/S2P/Onset interpreter 插件）落地前，L5 中相关用例按
  D-W3 配置期门控 + 目标缺失门控双闸处理；
- **接线骨架**（第一轮 §4.3 代码原样适用，变量名不变：`WOLF_G2P_PACKAGES_DIR` /
  `WOLF_G2P_PACKAGES_SOURCE` 逃生口 / `ENVIRONMENT` 注入 / 配置期门控）。

### 4.1 宿主模拟层 HostSim（D-W9）

E2E 测试统一经 `src/tests/support/HostSim.{h,cpp}` 执行，调用序列镜像宿主集成模式——
**锚定 lite `SynthrtEngine::initialize`（`src/libs/SynthrtEngine/SynthrtEngine.cpp:298`
起）的宿主语义**：lite 侧「插件路径注入（`g2pPluginPaths`）→ 官方包路径注入
（`officialG2pPackages`，部署源自 `InferEngine.cpp:219` 的 `srt-g2p/G2pPackages`）→
会话装配扫描（`VoicebankSession::refresh`）」的三段式，在 main 框架的等价物为：

| # | lite（refactor 栈，今日） | HostSim（main 框架，测试） |
| :-- | :-- | :-- |
| 1 | 链接 langCore/wolf-midi 等注册方 | 链接 wolf → `linguist` 类别静态注册（先于一切 unit 构造） |
| 2 | Runtime + LanguageService 装配 | `SynthUnit unit`（构造即收集全部注册类别） |
| 3 | `resources.g2pPluginPaths` 注入 | `unit.setPluginPaths("linguist", {wolf provider 插件目录})`（类别级，`SynthUnit.h:65-67`） |
| 4 | `resources.officialG2pPackages` 注入 | `unit.setPackagePaths({WOLF_G2P_PACKAGES_DIR, …})` |
| 5 | VoicebankSession 扫描声库 | `openPackage(path, OpenMode::Load)`：校验→依赖求解→提交 |
| 6 | LanguageRoute 选择语言模块 | 贡献解析 `package->contribution("linguist", id)->as<LinguistSpec>()` |
| 7 | 语言服务调用与释放 | `LinguistExecutive` 创建 / 父子销毁 |

HostSim 收敛为一个构造选项结构（`packagePaths`/`pluginPaths`）+ 少量断言助手，
供 §4.2 的 E2E 测试复用——测试验证的就是未来宿主真实的调用路径，而非测试自造的
旁路。lite 侧第 1-4 步的映射在 lite 迁 main 时按本表对照实施（§5 缓议项）。

### 4.2 测试项清单（实现轮直接照此落文件）

门控三轴：`[编译]` = `WOLF_BUILD_TESTS`；`[数据]` = `g2p-tests` feature（配置期
`find_package` 门控，D-W3）；`[插件]` = 目标 interpreter provider 在位。缺门即整体
不注册（配置期）或用例早退（运行期兜底）。

| 测试文件 | 门控 | 数据 | 用例要点 |
| :-- | :-- | :-- | :-- |
| `test_LinguistContrib`（既有） | 编译 | — | 类别注册、`LINGUIST_CATEGORY` 名 |
| `test_LinguistProvider`（既有） | 编译 | — | 插件 create 三元组契约、validators、超 level 拒绝 |
| `test_LinguistDeclaration`（新） | 编译 | 夹具合成 | DataOnly 正例加载；`configuration` 非空对象拒（`WolfLinguistProvider.cpp:308-319` 锚）；imports 缺 g2p/s2p 拒（基数下界，`:92-152` 锚）；目标 interface 不符拒 |
| `test_LinguistExports`（新） | 编译 | 夹具合成 | `exports.phonemes` 形状：元素空/重复/路径所指非数组 → provider 加载期形状校验拒绝（level-1 既定强制面） |
| `test_DependencyResolution`（新） | 编译 | 夹具合成 | Probe 求解：目标落 `[compatVersion, version]` 命中/不落缺依赖；同 id 多来源最高版本与路径序遮蔽（spec :386-406） |
| `test_CategoryNotRegistered`（新，独立 exe **不链 wolf**，`add_auto_test(... synthrt)` 即可） | 编译 | 夹具合成 | 未注册类别的 unit 打开含 linguist 贡献包 → 整包拒绝（`PackageLoader.cpp:1210-1212` 锚）——补上「链接即注册」的反向证明 |
| `test_PackageInventory`（新） | 编译+数据 | 真数据 | 逐 `WOLF_G2P_PACKAGES_SUITES`：desc.json 字段、`phonemes.json` 数组形状、声明所引资源在位（重入式，套件增删零测试改动） |
| `test_PackageLoadHostSim`（新） | 编译+数据+插件 | 真数据 | HostSim 全链 × `cmn`（algo-pinyin）与 `multi`（pipe-chain × multig2p-onnx）：Load 事务、linguist 贡献解析、`LinguistSpec`/exports 断言 |
| `test_ExecutiveLifecycle`（新） | 编译+数据+插件 | 真数据 | `LinguistExecutive` 创建与父子销毁；G2P/S2P/Onset interpreter 端到端用例随 provider 落地激活（在位前该段门控关闭） |

## 5. lite 侧（本轮零改动）

- g2p 包 port 在 wolf 私有层，lite 不可见——lite 继续消费共享 overlay synthrt port
  自带旧格式 `share/synthrt/G2pPackages`（refactor 线运行时面不变）；
- 第一轮 §2.6 的 lite 接线（bump 子模块 + manifest 依赖 + 部署拷贝）**整节缓议**，
  触发条件：lite 迁 main 线（届时 port 亦可考虑上提共享 overlay，配合第一轮 §2.5
  的提交序纪律）；
- 共享 overlay 本轮**零提交**（synthrt pin 不动、无新 port），lite 无感。

## 6. 全流程逐链路打通与实施时序

### 6.1 逐链路打通表（每链必有生产者、产物、消费者、验证口径）

| 链 | 生产者 → 产物 | 消费者 | 验证口径 |
| :-- | :-- | :-- | :-- |
| L1 | synthrt 夹具资源 + 仓内元数据（REVIEWED）→ `make-release.ps1` 装配 → zip | release 流程 | 脚本配对/desc.json 校验；dry-run（`-IncludeUnreviewed`）+ 抽样解包 diff 资源字节 |
| L2 | zip + `manifest.json` → tag `v<bundle>` → GitHub Release | port 下载 | 下载抽验 SHA512 对 manifest |
| L3 | `assets.cmake` → wolf 私有 `wolf-g2p-packages` port → `share/wolf/packages/` | wolf 测试（未来 lite） | 干净 `vcpkg install` 后目录在位、SHA 与 assets 一致 |
| L4 | port config 包 → `find_package` → `WOLF_G2P_PACKAGES_DIR`/`_SUITES`/逐套件变量 | CMake 测试装配 | configure 期 STATUS 输出 + feature 裁剪组合变量正确 |
| L5 | wolf 私有 `synthrt` port（main pin）→ install 树 → `find_package(synthrt)` + `SYNTHRT_MODULES_DIR` | wolf 库/插件/测试 | 干净 install + configure；`openPackage`/`ContribCategory` 符号可链接 |
| L6 | wolf manifest `g2p-tests` feature → port features（cmn/multi） | 测试数据面 | `--x-feature=g2p-tests` 装得对应套件；无 feature 时不下载 |
| L7 | wolf CMake 选项（`WOLF_BUILD_TESTS`）→ 测试目标注册矩阵（§4.2） | ctest | 开关×feature 四组合跑通：OFF=零测试、ON 无数据=夹具组、ON+数据=全组 |
| L8 | HostSim 序列 → `openPackage(Load)` → 贡献解析 → Executive | 测试断言 | `test_PackageLoadHostSim`/`test_ExecutiveLifecycle` 绿 |
| L9 | `wolf` port（默认关测试）→ install 树（lib+插件） | 未来宿主（lite 迁 main 时） | port 流水线绿、安装树无测试可执行文件 |
| — | lite 消费（部署拷贝 + 引擎接线） | lite | **缓议**（§5），触发=lite 迁 main |

打通确认的责任划分：L1-L2 的**运行时验证**落在实施第 3 步（首次 release 的 dry-run
与试载），L3-L8 落在实施第 1/4 步（port 安装与 ctest），本设计保证的是链路两端
接口一一咬合（变量名、布局、命名规则、feature 键在 §2.7/§3/§4 间已互相对齐）。

### 6.2 实施时序（每步独立可验证可回滚）

| # | 步骤 | 产出/验证 | 回滚 |
| :-- | :-- | :-- | :-- |
| 0 | 前置核对 | 共享 overlay 两仓 `git status` 干净；main pin 锚点复核 | 无副作用 |
| 1 | wolf 私有机制落地 | `vcpkg-local/ports/synthrt`（main pin）+ `wolf` port（D-W8）+ manifest 双层/依赖 + AGENTS.md + README 改写；独立 install root 干净 `vcpkg install`（含 L5/L9 验证） | 删私有层目录 + manifest 还原 |
| 2 | 资源仓元数据转换 | **骨架已完成（`613035a`）**；余：`-DeriveExports` 推导候选 → **用户逐套审定** → `packages/` 入库 + REVIEWED | 回退提交 |
| 3 | 首次 release | make-release（先 `-IncludeUnreviewed` dry-run 校验 L1）→ tag + GitHub Release；下载抽验 SHA + 解包 `openPackage(DataOnly)` 试载 | 删 release/tag |
| 4 | wolf g2p port + 测试 | 私有层增 `wolf-g2p-packages` port + manifest `g2p-tests` + CMake 接线 + §4.2 测试清单；L3/L4/L6/L7/L8 验证（四组合矩阵） | 分支回退 |
| 5 | 复盘回写 | D-W1'~W9 并入发行文档台账；wolf `docs/Status.md` 测试状态更新 | 文档独立 revert |

**纪律注记**：lite 侧本轮零改动（无 lite-link 联动面）；wolf 仓与新资源仓的推送在
实施时逐次确认；synthrt 本分支不 push；wolf 构建遵守 CLion cmake ≥ 4.x + vcvars +
ninja 沙箱后台提权（synthrt-build 教训同款）。

## 7. 实施核对项与开放问题

**实施核对项**：

- 私有层遮蔽方向实测（`vcpkg install` 后以 install 树内 synthrt 版本/来源验证
  first-wins 语义与文档一致）；
- main 经 port 构建的 config fixup 覆盖面（synthrt + dsinfer 两套 config，§1.2）；
- Multi 运行时键汇裁剪清单（§2.5，对照变体文档 §5）；
- 逐套件语言贡献 id 拟名表与 s2p 变体复核（§2.3/§2.5，随审定清单交付）；
- port `version-string` 四段式与 vcpkg 版本比较文法（上位文档 §9 既列）；
- `wolf` port 的插件交付面与 vcpkg 策略（`wolflinguistprovider` 插件 dll 的安装
  位置与政策开关，参照共享 overlay synthrt port 头部策略先例）；
- make-release dry-run（`-IncludeUnreviewed`）在元数据入库后、首次 tag 前执行，
  顺带校验 L1 链资源字节一致性。

**开放问题（备案）**：

- Num/Punc/Unknown 的「旧栈辅助片段」在 wolf 新栈的消费语义（待语言链真实需求）；
- 夹具退役时点：资源仓自持元数据后，synthrt 夹具仅余资源体，I2 时随 DSPK 化一并
  处置（上位文档 §8 I2 口径顺延）；
- port 上提共享 overlay 的时机（lite 迁 main 时，触发条件见 §5）。
