# SynthRT

SynthRT is a package-oriented runtime for singing voice synthesis. Packages describe the contributions they provide, while category implementations and plugins interpret those declarations and perform the actual work.

> [!WARNING]
> SynthRT is pre-release software. DS Spec 2.4 and the C++ API are still evolving together, so breaking changes currently have no deprecation period.

## Repository Contents

- **synthrt** provides the host runtime. It reads package manifests, resolves dependencies and contribution references, discovers interpreters, and manages package loading.
- **dsinfer** provides the DiffSinger-facing inference contracts and implementations. Its installed CMake target remains `dsinfer::dsinfer`.

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

Plugin bundles contain a shared library and a neighboring `plugin.json`. Build and installation paths follow this layout:

```text
plugins/
└── dsinfer/
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
- vcpkg for the manifest-managed dependencies

## Dependencies

The vcpkg manifest supplies the project's library and build dependencies:

- [qmsetup](https://github.com/stdware/qmsetup)
- [stdcorelib](https://github.com/stdware/stdcorelib)
- [stdcorelib.plugin](https://github.com/stdware/stdcorelib.plugin)
- [stduuid](https://github.com/mariusbancila/stduuid)
- [BLAKE3](https://github.com/BLAKE3-team/BLAKE3)
- [sparsepp](https://github.com/greg7mdp/sparsepp)
- [bit7z](https://github.com/rikyoz/bit7z)

Additional dependencies are required for particular targets:

- [ONNX Runtime](https://github.com/microsoft/onnxruntime) is required to build the ONNX inference driver.
- [Boost.Test](https://www.boost.org/doc/libs/release/libs/test/) is required when `SYNTHRT_BUILD_TESTS` is enabled.

## Prepare Dependencies

Run the following commands from the repository root.

### Windows

```powershell
git clone https://github.com/microsoft/vcpkg.git vcpkg
.\vcpkg\bootstrap-vcpkg.bat
.\vcpkg\vcpkg.exe install --x-manifest-root=scripts/vcpkg-manifest --x-install-root=vcpkg/installed --x-feature=tests
```

### Unix-Like Systems

```sh
git clone https://github.com/microsoft/vcpkg.git vcpkg
./vcpkg/bootstrap-vcpkg.sh
./vcpkg/vcpkg install \
    --x-manifest-root=scripts/vcpkg-manifest \
    --x-install-root=vcpkg/installed \
    --x-feature=tests
```

Omit `--x-feature=tests` if automated tests are not required.

## Prepare ONNX Runtime

ONNX Runtime is provided by the `onnxruntime-builds` overlay port. The `onnx` feature of the vcpkg manifest installs it together with the other dependencies:

```sh
vcpkg install --x-manifest-root=scripts/vcpkg-manifest --x-install-root=vcpkg/installed --x-feature=onnx
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
    -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DSYNTHRT_BUILD_TESTS=ON

cmake --build build/Release --target all
cmake --install build/Release
```

## Running the Automated Tests

`SYNTHRT_BUILD_TESTS=ON` registers the Boost.Test targets with CTest. On Windows the test
executables link Boost.Test and the vcpkg runtime libraries dynamically, so the loader cannot
find them unless the matching directories are on `PATH` first; without that, every test aborts
with `0xc0000135` (`STATUS_DLL_NOT_FOUND`) before it starts.

```powershell
$env:PATH = "vcpkg\installed\x64-windows\debug\bin;$env:PATH"
ctest --test-dir build/Release --output-on-failure
```

Add the prefix that actually holds the runtime libraries when it is not `vcpkg/installed`
(for example a second prefix that provides Boost), and add the `onnxruntime-builds` runtime
directory when the driver tests run against a locally installed ONNX Runtime.

## Consume the Libraries with CMake

### Host Runtime Only

```cmake
find_package(synthrt CONFIG REQUIRED)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE synthrt::synthrt)
```

### DiffSinger Inference Support

```cmake
find_package(dsinfer CONFIG REQUIRED)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE dsinfer::dsinfer)
```

Point `CMAKE_PREFIX_PATH` at the installation prefix when CMake cannot find the packages automatically. The dsinfer library file is named `synthrt-dsinfer`, while its public CMake target is `dsinfer::dsinfer`.

## Documentation

- [DS package specification 2.4](docs/ds-spec-2.4.md)
- [dsinfer level 1 revised contracts](docs/dsinfer-level-1-revised.md)
- [Development conventions](docs/Development.md)

## License

SynthRT is distributed under the [Apache License 2.0](LICENSE).
