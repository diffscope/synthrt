# Synthesizer Runtime

## Requirements

+ [nlohmann_json](https://github.com/nlohmann/json)
+ [stduuid](https://github.com/mariusbancila/stduuid)
+ [hash-library](https://github.com/stbrumme/hash-library)
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

```cmake
cd third-party && cmake [-Dep=gpu] -P ../scripts/setup-onnxruntime.cmake
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

## TODO

- Logging
- Documentation
- Unit Tests