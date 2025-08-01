# Synthesizer Runtime

## Requirements

+ [nlohmann_json](https://github.com/nlohmann/json)
+ [stduuid](https://github.com/mariusbancila/stduuid)
+ [BLAKE3](https://github.com/BLAKE3-team/BLAKE3)
+ [sparsepp](https://github.com/greg7mdp/sparsepp)
+ [qmsetup](https://github.com/stdware/qmsetup)
+ [syscmdline](https://github.com/SineStriker/syscmdline)
+ [stdcorelib](https://github.com/SineStriker/stdcorelib)

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

2. CMake Configure & Build

```sh
cmake -B build -G Ninja \
    -DCMAKE_PREFIX_PATH=<dir> \  # `synthrt` install directory
    -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build --target all
```

## TODO

- Logging
- Documentation
- Unit Tests