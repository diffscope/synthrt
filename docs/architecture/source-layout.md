# SynthRT Source Layout Plan

日期: 2026-07-07

定位: 本文档记录 `synthrt` 的目录重构方案。它补充 `docs/refactoring-vnext` 的 clean-break 架构计划,重点解决源码目录、公开头、插件、工具、测试和 CMake target folder 的组织问题。

## 1. 结论

采用接近 LLVM 的顶层用途轴:

```text
include/     public headers
lib/         generic runtime library implementations
domains/     domain-specific first-class libraries
plugins/     plugin implementations
tools/       user/developer executables
unittests/   single-library unit tests
tests/       smoke, integration, end-to-end tests
third-party/ bundled or imported dependencies
```

通用 SynthRT runtime 主线是:

```text
Core
Driver
S2P
G2P
C
```

DiffSinger 专用能力使用轻量 domain 边界:

```text
domains/ds-bank
domains/ds-infer
```

对外路径使用完整领域名 `diffsinger`,源码和 target 名保留较短的 `ds-*`。

## 2. Target Layout

```text
synthrt/
  CMakeLists.txt
  cmake/

  include/
    synthrt/
      Core/
      Driver/
      S2P/
      G2P/
      C/
    diffsinger/
      Bank/
      Infer/

  lib/
    Core/
    Driver/
    S2P/
    G2P/
      Support/
    C/

  domains/
    ds-bank/
      CMakeLists.txt
      lib/
      unittests/
    ds-infer/
      CMakeLists.txt
      lib/
      plugins/
      unittests/

  plugins/
    G2P/
      chain/
      lstm/
      mandarin/
      cantonese/
      ds-dict/
    Driver/
      onnx/
    diffsinger/
      acoustic/
      duration/
      pitch/
      variance/
      vocoder/
      singer-provider/

  tools/
    dspk-pack-cli/
    dsinfer-cli/

  unittests/
    Core/
    Driver/
    S2P/
    G2P/
    C/

  tests/
    common/
    smoke/
    integration/
    abi/
    packaging/

  third-party/
  docs/
    architecture/
```

## 3. Naming Rules

| Object | Rule | Example |
|---|---|---|
| Top-level purpose directory | lowercase | `include`, `lib`, `plugins` |
| Generic component directory | PascalCase | `lib/Core`, `lib/G2P` |
| Domain directory | kebab-case | `domains/ds-bank` |
| Plugin directory | lowercase capability name | `plugins/diffsinger/pitch` |
| Tool directory | executable-oriented CLI name | `tools/dspk-pack-cli` |

Tool-style executables use a `-cli` suffix. This keeps user/developer command targets visually distinct from libraries, plugins, and domain packages.

`ds` is the internal abbreviation for DiffSinger. `diffsinger` is the public-facing domain name for include paths, plugin paths, install components, and aggregate CMake concepts.

## 4. Public Headers

Generic SynthRT headers:

```text
include/synthrt/Core/
include/synthrt/Driver/
include/synthrt/S2P/
include/synthrt/G2P/
include/synthrt/C/
```

DiffSinger-specific headers:

```text
include/diffsinger/Bank/
include/diffsinger/Infer/
```

Example includes:

```cpp
#include <synthrt/Core/Core/Runtime.h>
#include <synthrt/G2P/LanguageService.h>
#include <diffsinger/Bank/PackageParser.h>
#include <diffsinger/Infer/dsinfer/Api/Inferences/Acoustic/1/AcousticApiL1.h>
```

## 5. Namespaces

Generic runtime namespaces remain under the existing ABI namespaces while the
directory layout uses the public `synthrt/` include prefix:

```cpp
namespace srt::core {}
namespace srt::driver {}
namespace srt::s2p {}
namespace srt::g2p {}
```

DiffSinger domain namespaces use the domain-local short form, without the generic SynthRT runtime prefix:

```cpp
namespace ds::bank {}
namespace ds::infer {}
```

Directory migration and namespace migration should not be forced into the same patch. Moving files first and changing namespaces later keeps review and regression isolation easier.

## 6. CMake Targets

vNext uses new target names instead of keeping old names as compatibility surfaces.

| Component | Real target | Alias |
|---|---|---|
| Core | `synthrt-core` | `synthrt::core` |
| Driver | `synthrt-driver` | `synthrt::driver` |
| S2P | `synthrt-s2p` | `synthrt::s2p` |
| G2P | `synthrt-g2p` | `synthrt::g2p` |
| C API | `synthrt-c` | `synthrt::c` |
| DS Bank | `srt-ds-bank` | `srt-ds::bank` |
| DS Infer | `srt-ds-infer` | `srt-ds::infer` |
| DiffSinger aggregate | `srt-diffsinger` interface target | `srt::diffsinger` |

Plugin target naming after migration:

```text
synthrt-plugin-g2p-chain
synthrt-plugin-g2p-lstm
synthrt-plugin-g2p-mandarin
synthrt-plugin-g2p-cantonese
synthrt-plugin-g2p-ds-dict

synthrt-plugin-diffsinger-acoustic
synthrt-plugin-diffsinger-duration
synthrt-plugin-diffsinger-pitch
synthrt-plugin-diffsinger-variance
synthrt-plugin-diffsinger-vocoder
synthrt-plugin-diffsinger-singer-provider
```

Unit test target naming uses the vNext `synthrt-unittest-*` family:

```text
synthrt-unittest-core
synthrt-unittest-driver
synthrt-unittest-s2p
synthrt-unittest-g2p
synthrt-unittest-c
tst-ds-bank
tst-ds-infer
```

## 7. CMake Folder Policy

IDE target folders should not mirror the whole source tree. Use only broad categories:

```text
Libraries
Domains
Plugins
Tools
Tests
ThirdParty
```

Mapping:

| Target kind | CMAKE_FOLDER |
|---|---|
| Generic runtime libraries | `Libraries` |
| DS domain libraries | `Domains` |
| Plugins | `Plugins` |
| Executable tools | `Tools` |
| Tests | `Tests` |
| Third-party targets | `ThirdParty` |

## 8. Add Subdirectory Order

Top-level CMake should express dependency order rather than historical layout:

```cmake
add_subdirectory(third-party)

add_subdirectory(lib/Core)
add_subdirectory(lib/Driver)
add_subdirectory(lib/S2P)
add_subdirectory(lib/G2P)

add_subdirectory(domains/ds-bank)
add_subdirectory(domains/ds-infer)

add_subdirectory(lib/C)

add_subdirectory(plugins)
add_subdirectory(tools)

if(SYNTHRT_BUILD_TESTS)
    add_subdirectory(unittests)
    add_subdirectory(tests)
endif()
```

The exact order may be adjusted for real dependencies, but new ordering should remain dependency-driven.

## 9. Path Mapping

### Generic Runtime

| Current path | Target path |
|---|---|
| `modules/srt-core/include` | `include/synthrt/Core` |
| `modules/srt-core/lib` | `lib/Core` |
| `modules/srt-core/tests` | `unittests/Core` |
| `modules/srt-driver/include` | `include/synthrt/Driver` |
| `modules/srt-driver/lib` | `lib/Driver` |
| `modules/srt-driver/plugins/onnx` | `plugins/Driver/onnx` |
| `modules/srt-driver/tests` | `unittests/Driver` |
| `modules/srt-s2p/include` | `include/synthrt/S2P` |
| `modules/srt-s2p/lib` | `lib/S2P` |
| `modules/srt-s2p/tests` | `unittests/S2P` |
| `modules/srt-g2p/include` | `include/synthrt/G2P` |
| `modules/srt-g2p/lib` | `lib/G2P` |
| `modules/srt-g2p/tests` | `unittests/G2P` |
| `modules/srt-c/lib` | `lib/C` |
| `modules/srt-c/tests` | `unittests/C` |

### G2P Plugins

| Current path | Target path |
|---|---|
| `modules/srt-g2p/plugins/Utils/InferUtil` | `lib/G2P/Support` |
| `modules/srt-g2p/plugins/G2ps/Common` | `lib/G2P/Support` |
| `modules/srt-g2p/plugins/G2ps/ChainG2p` | `plugins/G2P/chain` |
| `modules/srt-g2p/plugins/G2ps/LstmG2p` | `plugins/G2P/lstm` |
| `modules/srt-g2p/plugins/G2ps/MandarinG2p` | `plugins/G2P/mandarin` |
| `modules/srt-g2p/plugins/G2ps/CantoneseG2p` | `plugins/G2P/cantonese` |
| `modules/srt-g2p/plugins/Dicts/DsDict` | `plugins/G2P/ds-dict` |

Plugin output uses a sibling `_shared` directory for runtime-provided common DLLs. Plugin-private dependencies and resources remain in the concrete plugin directory.

### DiffSinger Domain

| Current path | Target path |
|---|---|
| `dsbank/include/dsbank` | `include/diffsinger/Bank` |
| `dsbank/lib` | `domains/ds-bank/lib` |
| `dsbank/tests` | `domains/ds-bank/unittests` |
| `dsbank/tools/dspk-pack` | `tools/dspk-pack-cli` |
| `dsinfer/include` | `include/diffsinger/Infer` |
| `dsinfer/lib` | `domains/ds-infer/lib` |
| `dsinfer/tests` | `domains/ds-infer/unittests` |
| `dsinfer/tools/cli` | `tools/dsinfer-cli` |
| `dsinfer/plugins/inferenceinterpreters/acoustic` | `plugins/diffsinger/acoustic` |
| `dsinfer/plugins/inferenceinterpreters/duration` | `plugins/diffsinger/duration` |
| `dsinfer/plugins/inferenceinterpreters/pitch` | `plugins/diffsinger/pitch` |
| `dsinfer/plugins/inferenceinterpreters/variance` | `plugins/diffsinger/variance` |
| `dsinfer/plugins/inferenceinterpreters/vocoder` | `plugins/diffsinger/vocoder` |
| `dsinfer/plugins/singerproviders/diffsinger` | `plugins/diffsinger/singer-provider` |

### Removed Legacy Runtime

The old `synthrt` runtime is not assigned a final production path. During the refactor it may be kept only as unlinked reference input with an explicit deletion checkpoint. It must not be moved into a long-lived `legacy` production directory.

## 10. Clean-Break Phases

### Phase 1: New build graph

- Create final top-level directories.
- Add vNext generic `synthrt-*` targets and `synthrt::*` aliases.
- Add vNext DiffSinger `srt-ds-*` targets, `srt-ds::*` aliases, and `srt::diffsinger` aggregate interface target only for DS domain libraries.
- Do not link old runtime targets from new production targets.
- Maintain this document and `docs/refactoring-vnext` together.

### Phase 2: Generic runtime layout

- Move generic runtime implementation into `include/synthrt`, `lib`, `plugins`, and `unittests` under vNext `synthrt-*` targets.
- Update CMake paths.
- Update internal includes.
- Do not combine this with namespace rewrites unless the diff remains small.

### Phase 3: G2P plugin flattening

- Move real plugins under `plugins/G2P`.
- Move non-plugin support code under `lib/G2P/Support`.
- Normalize plugin target names to `synthrt-plugin-*`.

### Phase 4: DiffSinger domains

- Move DS public headers to `include/diffsinger`.
- Move `dsbank` implementation to `domains/ds-bank`.
- Move `dsinfer` implementation to `domains/ds-infer`.
- Move DS plugins to `plugins/diffsinger`.
- Move tools to `tools/dspk-pack-cli` and `tools/dsinfer-cli`.

### Phase 5: Legacy deletion

- Remove old v2 `synthrt` from the normal build graph.
- Delete compatibility adapters and conversion layers after their tests are replaced.
- Fail validation if vNext production targets include old runtime headers.

## 11. Validation

Every phase should pass:

```text
CMake configure
full build
SYNTHRT_BUILD_TESTS=ON test run
install tree inspection when install rules are affected
exported target smoke test when aliases or packages are affected
```

## 12. Risks

Avoid combining these changes in one patch:

| Combined change | Risk |
|---|---|
| Move directories + rename namespaces | Large review surface and hard-to-debug errors |
| Move plugins + change plugin IDs | Runtime loading regressions |
| Move headers + keep long-lived forwarding headers | Hidden compatibility layer contrary to current policy |
| Keep legacy linked while adding vNext APIs | Legacy becomes part of the new architecture by accident |

The preferred style is small, buildable phases that converge on the clean-break architecture. Compatibility layers require a named shipped contract and a deletion date.
