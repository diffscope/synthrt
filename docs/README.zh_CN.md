# SynthRT

[English](../README.md)

SynthRT 是面向包的歌声合成运行时。包描述自己提供的功能贡献，由类别实现与插件解释这些声明并完成实际工作。

> [!WARNING]
> SynthRT 是尚未发布的软件。DS Spec 2.4 与 C++ API 仍在同步演进，目前的破坏性改动没有弃用过渡期。

## 仓库内容

- **synthrt** 提供宿主运行时。它读取包清单，解析依赖与贡献引用，发现解释器，并管理包的加载。
- **dsinfer** 提供面向 DiffSinger 的推理契约与实现。它安装为 CMake 包 `synthrt-dsinfer`，目标为 `synthrt::dsinfer`。

## 运行时模型

### 包与贡献

- 目前一个包就是一个已安装的目录，其中含有清单 `desc.json`。
- 包由包 ID 与版本标识。版本由一至四段数字组成，右侧补齐后逐段按整数比较。
- 根对象的 `contributions` 按贡献类别对声明分组。
- 目前的内置类别有 `inference` 与 `singer`。
- 应用可以在加载第一个包之前注册更多类别。
- 一项贡献可以使用外部的声明文件，也可以整个写成 `desc.json` 中的一个条目，取决于它的类别。

贡献定位符不含包版本。依赖求解先选定包的版本，之后才绑定贡献引用。

```text
vendor/sample:inference/acoustic
vendor/sample:singer/main
:singer/main
```

- `vendor/sample:inference/acoustic` 指已求解的直接依赖 `vendor/sample` 中的 `acoustic`。
- `:singer/main` 指当前包中的 `main`。
- 一个定位符指向一项贡献。包的依赖要求使用 `desc.json` 中单独的 `id` 与 `version` 字段。

### 打开包

`SynthUnit` 提供两种打开包的模式：

1. `DataOnly` 只读取包的数据，不发现、不加载插件，也不把包发布到运行时状态中。
2. `Load` 执行完整的加载事务：
   1. Probe：探查包的声明，发现匹配的插件但不加载它们。
   2. Acquire：取得所需的运行时资源。
   3. Ready：校验每项贡献都已就绪。
   4. Commit：原子地提交整个包图。

Commit 之前的任何一步失败，包都不会以已加载的状态出现。已提交的包及其选定的插件在这次加载的整个生命周期内保持不变。

### 插件角色

- `SingerProviderPlugin` 创建歌手 provider。
- `InferenceInterpreterPlugin` 为请求的 `(interface, level, variant)` 契约创建解释器。
- `InferenceDriverPlugin` 创建按 backend 元数据选择的模型执行驱动，例如 `onnxdriver` 提供的 `onnx` backend。
- 一个插件 bundle 可以声明并创建不止一种契约。
- 每个插件类别有各自的有序搜索路径。

插件 bundle 由一个动态库和与它相邻的 `plugin.json` 组成。每个库的插件都位于它自己的库目录旁，构建树与安装树相同：

```text
lib/
└── synthrt-dsinfer/
    └── plugins/
        ├── inferencedrivers/
        │   └── onnx/
        ├── inferenceinterpreters/
        │   ├── acoustic/
        │   ├── duration/
        │   ├── pitch/
        │   ├── variance/
        │   └── vocoder/
        └── singerproviders/
            └── diffsinger/
```

宿主把 dsinfer 安装前缀下 `lib/synthrt-dsinfer/plugins` 里的各类别目录传给 `SynthUnit::setPluginPaths()`。一个类别的解释器来自多个库时，宿主在同一次调用中列出每个库的目录：

```cpp
const auto plugins = prefix / "lib" / "synthrt-dsinfer" / "plugins";
unit.setPluginPaths("inference", {plugins / "inferenceinterpreters"});
unit.setPluginPaths("singer", {plugins / "singerproviders"});
```

CMake 包把每个安装的插件导出为导入目标，例如 `synthrt::acoustic` 与 `synthrt::onnxdriver`。需要部署插件或取得某个插件目录的 CMake 工程用 `$<TARGET_FILE_DIR:synthrt::onnxdriver>` 读取，vcpkg 安装的 Debug 树也由它解析。

## 当前状态

包加载的基础部分目前已经实现：

- `DataOnly` 清单检查
- 内部的 Probe 与插件发现
- 依赖选择与贡献定位符绑定
- Acquire 与 Ready 校验
- 原子的 Commit 可见性
- 共享的包身份，以及基于强引用的释放

以下工作仍在进行中：

- 补全运行时执行与 import binding 的生命周期
- 把 dsinfer 其余的工具与命令行路径迁移到新的类型化 API
- 稳定解释器、歌手 provider 与推理驱动的实现
- 在基于目录的包加载之外，单独实现 `.dspk` 的安装

维护中的任务清单见[项目状态](Status.md)。

## 构建要求

- CMake 3.19 或更高版本
- 支持 C++17 的编译器
- Ninja 或其他 CMake 支持的构建工具
- Git，用于检出 vcpkg 及其 overlay，以安装清单管理的依赖

## 依赖

vcpkg 清单提供项目的库依赖与构建依赖：

- [qmsetup](https://github.com/stdware/qmsetup)
- [stdcorelib](https://github.com/stdware/stdcorelib)
- [stdcorelib.plugin](https://github.com/stdware/stdcorelib.plugin)
- [BLAKE3](https://github.com/BLAKE3-team/BLAKE3)
- [sparsepp](https://github.com/greg7mdp/sparsepp)
- [bit7z](https://github.com/rikyoz/bit7z)

特定目标还需要额外的依赖：

- 构建 ONNX 推理驱动需要 [ONNX Runtime](https://github.com/microsoft/onnxruntime)。
- 启用 `SYNTHRT_BUILD_TESTS` 时需要 [Boost.Test](https://www.boost.org/doc/libs/release/libs/test/)。

## 准备依赖

依赖由 [vcpkg-workspace.cmake](https://github.com/stdware/dev-scripts/blob/main/docs/about-scripts/vcpkg-workspace.md) 安装，该脚本下载到 `scripts/` 中。它把 `scripts/vcpkg-sources.json` 指定的 vcpkg 与 overlay 检出到 `deps/`，并把依赖安装在那里。在仓库根目录执行下列命令：

```sh
curl -fsSL -o scripts/vcpkg-workspace.cmake https://raw.githubusercontent.com/stdware/dev-scripts/main/scripts/vcpkg-workspace.cmake
cmake -P scripts/vcpkg-workspace.cmake install -- --x-feature=tests
```

在 Windows 上，可以在 PowerShell 或 cmd 中这样下载脚本：

```bat
powershell -Command "Invoke-WebRequest -OutFile scripts/vcpkg-workspace.cmake https://raw.githubusercontent.com/stdware/dev-scripts/main/scripts/vcpkg-workspace.cmake"
```

不需要自动测试时，去掉 `-- --x-feature=tests`。`cmake -P scripts/vcpkg-workspace.cmake help` 列出其余命令，`--dry-run` 只打印命令而不执行。`scripts/vcpkg-sources.local.json` 优先于 `scripts/vcpkg-sources.json`，例如可以用它指向本地的 overlay。

## 准备 ONNX Runtime

ONNX Runtime 由 overlay 端口 `onnxruntime-builds` 提供。vcpkg 清单的 `onnx` feature 会把它与其他依赖一起安装：

```sh
cmake -P scripts/vcpkg-workspace.cmake install -- --x-feature=tests --x-feature=onnx
```

ONNX Runtime 的 CUDA 12 构建是该端口的 `cuda12` feature，即 `onnxruntime-builds[cuda12]`。

本仓库不指定 ONNX Runtime 的版本。安装哪个版本只由 overlay 端口决定，所以换一份 ONNX Runtime 载荷不需要改动本仓库。没有安装 `onnx` feature 时，CMake 会跳过 ONNX 工具库与驱动目标，其余库照常构建。

构建不会把 ONNX Runtime 的库部署到驱动插件旁边。驱动从宿主在 `DriverInitArgs::runtimePath` 中传入的目录加载 ONNX Runtime，所以 ONNX Runtime 在运行时的位置由宿主应用负责。本仓库的测试传入 `onnxruntime-builds` 包定义的 `ONNXRUNTIME_BUILDS_RUNTIME_DIR`。

## 从源码构建

下面的示例使用单配置的 Ninja 构建：

```sh
cmake -S . -B build/Release -G Ninja \
    -DCMAKE_INSTALL_PREFIX=<install-dir> \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=deps/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DSYNTHRT_BUILD_TESTS=ON

cmake --build build/Release --target all
cmake --install build/Release
```

## 运行自动测试

`SYNTHRT_BUILD_TESTS=ON` 把 Boost.Test 目标注册到 CTest。在 Windows 上，测试程序动态链接 Boost.Test 与 vcpkg 的运行库，必须先把对应目录加入 `PATH`，加载器才能找到它们。否则每个测试都会在启动前以 `0xc0000135`（`STATUS_DLL_NOT_FOUND`）中止。

```powershell
$env:PATH = "deps\vcpkg\installed\x64-windows\bin;$env:PATH"
ctest --test-dir build/Release --output-on-failure
```

Debug 构建改用 `deps\vcpkg\installed\x64-windows\debug\bin` 中的运行库。

dsinfer 的 `tests/auto/plugins` 是单个插件的测试，每个头文件一个 `test_<头文件名>.cpp`，像测试库的类那样链接插件的对象库。

## 用 CMake 使用这些库

### 只用宿主运行时

```cmake
find_package(synthrt CONFIG REQUIRED)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE synthrt::synthrt)
```

### 支持 DiffSinger 推理

```cmake
find_package(synthrt-dsinfer CONFIG REQUIRED)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE synthrt::dsinfer)
```

CMake 不能自动找到这些包时，把 `CMAKE_PREFIX_PATH` 指向安装前缀。dsinfer 的库文件、CMake 包与安装目录都命名为 `synthrt-dsinfer`，目标保留短名，为 `synthrt::dsinfer`。

## 文档

- [DS 包规范 2.4](DsSpec2.4.md)
- [dsinfer Level 1 修订版契约](DsInferLevel1.md)
- [开发指南](DevHelp.md)
- [开发约定](Development.md)

## 许可证

Apache License 2.0，见 [LICENSE](../LICENSE)。