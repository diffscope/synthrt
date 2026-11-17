# SynthRT

[中文](docs/README.zh_CN.md)

SynthRT is a package-oriented runtime for singing voice synthesis. Packages describe the contributions they provide, while category implementations and plugins interpret those declarations and perform the actual work.

> [!WARNING]
> SynthRT is pre-release software. DS Spec 2.4 and the C++ API are still evolving together, so breaking changes currently have no deprecation period.

## Repository Contents

- **synthrt** provides the host runtime. It reads package manifests, resolves dependencies and contribution references, discovers interpreters, and manages package loading.
- **dsinfer** provides the DiffSinger-facing inference contracts and implementations. It installs as the CMake package `synthrt-dsinfer` and its target is `synthrt::dsinfer`.

## Runtime Model

### Packages and Contributions

- A package is currently an installed directory containing a `desc.json` manifest.
- A package is identified by its package ID and a version of one to four numeric parts, compared as integers after padding on the right.
- The root `contributions` object groups declarations by contribution category.
- Built-in categories currently include `inference` and `singer`.
- Applications may register additional categories before loading their first package.
- A contribution may use an external declaration file or exist entirely as an entry in `desc.json`, depending on its category.

Contribution locators do not contain package versions. Dependency resolution selects package versions before contribution references are bound.

```text
vendor/sample:inference/acoustic
vendor/sample:singer/main
:singer/main
```

- `vendor/sample:inference/acoustic` refers to `acoustic` in the already-resolved direct dependency `vendor/sample`.
- `:singer/main` refers to `main` in the current package.
- A locator names one contribution. Package dependency requirements use the separate `id` and `version` fields in `desc.json`.

### Package Opening

`SynthUnit` exposes two package opening modes:

1. `DataOnly` reads package data without discovering or loading plugins and without publishing the package into runtime state.
2. `Load` performs the full load transaction:
   1. Probe package declarations and discover matching plugins without loading them.
   2. Acquire the required runtime resources.
   3. Validate that every contribution is ready.
   4. Commit the complete package graph atomically.

If any step before Commit fails, the package is not made visible as loaded. The committed package and its resolved plugin choices remain stable for the lifetime of that load.

### Plugin Roles

- `SingerProviderPlugin` creates singer providers.
- `InferenceInterpreterPlugin` creates interpreters for a requested `(interface, level, variant)` contract.
- `InferenceDriverPlugin` creates model execution drivers selected by backend metadata, such as the `onnx` backend supplied by `onnxdriver`.
- One plugin bundle may advertise and create more than one supported contract.
- Each plugin category has its own ordered search path sequence.

Plugin bundles contain a shared library and a neighboring `plugin.json`. Each library keeps its plugins beside its own library directory, in the build tree and in the install tree alike:

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

A host passes the category directories under `lib/synthrt-dsinfer/plugins` of the prefix that it installs dsinfer into to `SynthUnit::setPluginPaths()`. When several libraries ship interpreters of one category, the host lists the directory of each in the same call:

```cpp
const auto plugins = prefix / "lib" / "synthrt-dsinfer" / "plugins";
unit.setPluginPaths("inference", {plugins / "inferenceinterpreters"});
unit.setPluginPaths("singer", {plugins / "singerproviders"});
```

The CMake package exports every installed plugin as an imported target, such as `synthrt::acoustic` and `synthrt::onnxdriver`. A CMake project that deploys the plugins, or needs the directory of one, reads it with `$<TARGET_FILE_DIR:synthrt::onnxdriver>`, which also resolves the Debug tree of a vcpkg installation.

## Current Status

The package-loading foundation currently implements:

- `DataOnly` manifest inspection
- internal Probe and plugin discovery
- dependency selection and contribution locator binding
- Acquire and Ready validation
- atomic Commit visibility
- shared package identities and strong-reference-based release

The following work is still in progress:

- closing the runtime execution and import-binding lifecycle
- migrating the remaining dsinfer utilities and command-line paths to the new typed APIs
- stabilizing the interpreter, singer provider, and inference driver implementations
- implementing `.dspk` installation separately from directory-based package loading

See [Project status](docs/Status.md) for the maintained task list.

## Build Requirements

- CMake 3.19 or later
- A C++17-capable compiler
- Ninja or another CMake-supported build tool
- Git, with which vcpkg and its overlays are checked out for the manifest-managed dependencies

## Dependencies

The vcpkg manifest supplies the project's library and build dependencies:

- [qmsetup](https://github.com/stdware/qmsetup)
- [stdcorelib](https://github.com/stdware/stdcorelib)
- [stdcorelib.plugin](https://github.com/stdware/stdcorelib.plugin)
- [BLAKE3](https://github.com/BLAKE3-team/BLAKE3)
- [sparsepp](https://github.com/greg7mdp/sparsepp)
- [bit7z](https://github.com/rikyoz/bit7z)

Additional dependencies are required for particular targets:

- [ONNX Runtime](https://github.com/microsoft/onnxruntime) is required to build the ONNX inference driver.
- [Boost.Test](https://www.boost.org/doc/libs/release/libs/test/) is required when `SYNTHRT_BUILD_TESTS` is enabled.

## Prepare Dependencies

The dependencies are installed with [vcpkg-workspace.cmake](https://github.com/stdware/dev-scripts/blob/main/docs/about-scripts/vcpkg-workspace.md), downloaded into `scripts/`. The script checks out vcpkg and the overlays that `scripts/vcpkg-sources.json` specifies into `deps/`, and installs the dependencies there. Run the following commands from the repository root.

```sh
curl -fsSL -o scripts/vcpkg-workspace.cmake https://raw.githubusercontent.com/stdware/dev-scripts/main/scripts/vcpkg-workspace.cmake
cmake -P scripts/vcpkg-workspace.cmake install -- --x-feature=tests
```

On Windows, the script is downloaded in PowerShell or in cmd with

```bat
powershell -Command "Invoke-WebRequest -OutFile scripts/vcpkg-workspace.cmake https://raw.githubusercontent.com/stdware/dev-scripts/main/scripts/vcpkg-workspace.cmake"
```

Omit `-- --x-feature=tests` if automated tests are not required. `cmake -P scripts/vcpkg-workspace.cmake help` lists the other commands. `--dry-run` prints the commands without running them. `scripts/vcpkg-sources.local.json` takes precedence over `scripts/vcpkg-sources.json`, for example to use a local overlay.

## Prepare ONNX Runtime

ONNX Runtime is provided by the `onnxruntime-builds` overlay port. The `onnx` feature of the vcpkg manifest installs it together with the other dependencies:

```sh
cmake -P scripts/vcpkg-workspace.cmake install -- --x-feature=tests --x-feature=onnx
```

The CUDA 12 build of ONNX Runtime is the `cuda12` feature of that port, `onnxruntime-builds[cuda12]`.

This repository specifies no ONNX Runtime version. The overlay port alone determines the installed version, so a different ONNX Runtime payload requires no change to this repository. If the `onnx` feature is not installed, CMake omits the ONNX utility and driver targets, and the remaining libraries can still be built.

The build deploys no ONNX Runtime libraries beside the driver plugin. The driver loads ONNX Runtime from the directory that the host passes in `DriverInitArgs::runtimePath`, so the run-time location of ONNX Runtime is the responsibility of the host application. The tests in this repository pass `ONNXRUNTIME_BUILDS_RUNTIME_DIR`, which the `onnxruntime-builds` package defines.

## Build From Source

The following example uses a single-configuration Ninja build:

```sh
cmake -S . -B build/Release -G Ninja \
    -DCMAKE_INSTALL_PREFIX=<install-dir> \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=deps/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DSYNTHRT_BUILD_TESTS=ON

cmake --build build/Release --target all
cmake --install build/Release
```

## Running the Automated Tests

`SYNTHRT_BUILD_TESTS=ON` registers the Boost.Test targets with CTest. On Windows the test executables link Boost.Test and the vcpkg runtime libraries dynamically, so the loader cannot find them unless the matching directory is on `PATH` first. Without it, every test aborts with `0xc0000135` (`STATUS_DLL_NOT_FOUND`) before it starts.

```powershell
$env:PATH = "deps\vcpkg\installed\x64-windows\bin;$env:PATH"
ctest --test-dir build/Release --output-on-failure
```

A Debug build takes the runtime libraries from `deps\vcpkg\installed\x64-windows\debug\bin` instead.

`tests/auto/plugins` of dsinfer holds the tests of single plugins, one `test_<header>.cpp` for each header, which links the objects of the plugin as the test of a library class links the library.

## Consume the Libraries with CMake

### Host Runtime Only

```cmake
find_package(synthrt CONFIG REQUIRED)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE synthrt::synthrt)
```

### DiffSinger Inference Support

```cmake
find_package(synthrt-dsinfer CONFIG REQUIRED)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE synthrt::dsinfer)
```

Point `CMAKE_PREFIX_PATH` at the installation prefix when CMake cannot find the packages automatically. The dsinfer library file, its CMake package and its install directories are all named `synthrt-dsinfer`, while the target keeps the short name, `synthrt::dsinfer`.

## Documentation

- [DS package specification 2.4](docs/ds-spec-2.4.md)
- [dsinfer level 1 revised contracts](docs/dsinfer-level-1-revised.md)
- [Development guide](docs/DevHelp.md)
- [Development conventions](docs/Development.md)

## License

Apache License 2.0. See [LICENSE](LICENSE).