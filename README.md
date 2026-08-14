# Synthesis Runtime

A runtime for singing voice synthesis, built around packages that declare what they contribute and plugins that know how to run it.

The repository holds two libraries. **synthrt** is the host: it loads packages, resolves what they declare against each other, and finds the plugins that implement them. It knows nothing about how a voice is synthesized. **dsinfer** is the DiffSinger implementation on top of it, with the inference stages, the ONNX driver and the singer provider that turn a package into audio.

Nothing here is a program. Both are libraries for an editor or a tool to embed.

## How it fits together

A **package** is a directory with a `desc.json` naming what it contributes. Two kinds exist today:

- a **singer**, a voice, which imports the inferences it needs
- an **inference**, one stage of synthesis, which names the interpreter class that can run it

Packages refer to each other, and to what is inside them, through one syntax:

```
vendor/sample=1.0.0.0:inference/acoustic    # fully qualified
vendor/sample=1.0.0.0:singer/main
vendor/sample:singer/main                   # version resolved from the dependencies
:singer/main                                # within the package doing the referring
vendor/sample=1.0.0.0                       # the package itself
```

Loading a package is a transaction. Every contribute it declares goes through `Invalid -> Initialized -> Ready -> Finished`, and a failure at any point rolls the earlier ones back in reverse. A package that names an inference it cannot resolve does not half-load.

Three plugin interfaces sit between the host and the work:

| Interface | Answers |
|---|---|
| `SingerProviderPlugin` | how to read a voice of a given class |
| `InferenceInterpreterPlugin` | how to run one inference stage |
| `InferenceDriverPlugin` | how to execute a model at all |

A plugin is a shared library exporting `synthrt_plugin_instance`, found by interface id and key. The ones shipped here are the DiffSinger singer provider, five interpreters (acoustic, duration, pitch, variance, vocoder) and the ONNX driver.

The package format is specified separately, at [dspk.diffscope.org](https://dspk.diffscope.org/docs/1.0/package-specification.html). `docs/` holds the DiffSinger-side notes: `ds-spec-2.3.md` and `dsinfer-level-1-draft.md`.

## Requirements

CMake 3.19 or later.

+ [nlohmann_json](https://github.com/nlohmann/json)
+ [stduuid](https://github.com/mariusbancila/stduuid)
+ [BLAKE3](https://github.com/BLAKE3-team/BLAKE3)
+ [sparsepp](https://github.com/greg7mdp/sparsepp)
+ [qmsetup](https://github.com/stdware/qmsetup)
+ [stdcorelib](https://github.com/stdware/stdcorelib)

ONNX Runtime is fetched separately, see below. Boost.Test is needed only to build the tests.

## Setup Environment

### VCPKG Packages

#### Windows
```sh
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat

vcpkg install --x-manifest-root=../scripts/vcpkg-manifest --x-install-root=./installed
```

#### Unix
```sh
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh

./vcpkg install \
    --x-manifest-root=../scripts/vcpkg-manifest \
    --x-install-root=./installed
```

Add `--x-feature=tests` to either to bring in Boost.Test.

### Install OnnxRuntime

Default configuration (no CUDA support):

```cmake
cd third-party && cmake -P ../scripts/setup-onnxruntime.cmake
```

With CUDA 11.x support:

```cmake
cd third-party && cmake -Dep=cuda11 -P ../scripts/setup-onnxruntime.cmake
```

With CUDA 12.x support:

```cmake
cd third-party && cmake -Dep=cuda12 -P ../scripts/setup-onnxruntime.cmake
```

### Build & Install

The buildsystem is able to deploy the shared libraries to build directory and install directory automatically.

```sh
cmake -B build -G Ninja \
    -DCMAKE_INSTALL_PREFIX=<dir> \  # install directory
    -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DQMSETUP_APPLOCAL_DEPS_PATHS_DEBUG=vcpkg/installed/<triplet>/debug/<runtime> \
    -DQMSETUP_APPLOCAL_DEPS_PATHS_RELEASE=vcpkg/installed/<triplet>/<runtime> \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build --target all

cmake --build build --target install
```

## How to Use

1. CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)

project(example)

find_package(dsinfer CONFIG REQUIRED)
add_executable(example main.cpp)
target_link_libraries(example dsinfer::dsinfer)
```

Both libraries install to the same prefix, so pointing `CMAKE_PREFIX_PATH` at it finds either. Link `synthrt::synthrt` alone for the host without the DiffSinger implementation.

2. CMake Configure & Build

```sh
cmake -B build -G Ninja \
    -DCMAKE_PREFIX_PATH=<dir> \  # `synthrt` install directory
    -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build --target all
```