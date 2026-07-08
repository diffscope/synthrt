# C++ Project Layout Guide

日期: 2026-07-07

定位: 本文档是一份可复用的 C++ 项目目录重构指南。它抽象自 `synthrt` 的目录重构讨论,适用于包含多个库、插件、工具、测试和 legacy 代码的中大型 C++ 项目。

## 1. Design Goal

目录结构应该回答四个问题:

```text
What is public API?
What is implementation?
What is a plugin?
What is a tool, test, domain extension, or legacy component?
```

不要让历史模块名、构建目标名、插件类型名和产品名混在同一层级。大型 C++ 项目更适合先按用途建立稳定顶层轴,再在每个用途下按组件划分。

## 2. Recommended Layout

```text
project/
  CMakeLists.txt
  cmake/

  include/
    project/
      Core/
      Support/
      Runtime/
      ComponentA/
      ComponentB/
    domain-name/
      DomainComponentA/
      DomainComponentB/

  lib/
    Core/
    Support/
    Runtime/
    ComponentA/
    ComponentB/

  domains/
    domain-a/
      CMakeLists.txt
      lib/
      plugins/
      unittests/
    domain-b/
      CMakeLists.txt
      lib/
      plugins/
      unittests/

  plugins/
    host-a/
      plugin-a/
      plugin-b/
    domain-name/
      plugin-c/
      plugin-d/

  tools/
    tool-a/
    tool-b/

  unittests/
    Core/
    Runtime/
    ComponentA/

  tests/
    smoke/
    integration/
    e2e/

  examples/
  docs/
    architecture/

  third-party/
```

This is LLVM-inspired, not LLVM-copying. The important idea is the stable top-level purpose axis.

## 3. Top-Level Directory Rules

| Directory | Purpose |
|---|---|
| `include` | Public installable headers |
| `lib` | Main library implementations |
| `domains` | Important but domain-specific first-class capabilities |
| `plugins` | Runtime-loadable or registration-based plugin implementations |
| `tools` | Executables shipped to users or developers |
| `unittests` | Single-library unit tests |
| `tests` | Smoke, integration, and end-to-end tests |
| `examples` | Small usage examples |
| `docs` | Architecture and user documentation |
| `legacy-delete` | Temporary reference-only old code with deletion checkpoint |
| `third-party` | Bundled or imported third-party code |

Prefer a short, stable top-level list. A source tree should not need a new top-level directory every time a component is added.

## 4. Component Placement

Put a component in `lib` when it is part of the generic product runtime:

```text
lib/Core
lib/Runtime
lib/Driver
lib/Serialization
```

Put a component in `domains` when it is important enough to be first-class but conceptually specific:

```text
domains/diff-singer
domains/game-audio
domains/medical-imaging
domains/vendor-format
```

Put a component in `plugins` when it is loaded, registered, discovered, or selected through a plugin boundary:

```text
plugins/audio/wav
plugins/audio/flac
plugins/inference/onnx
plugins/domain-name/special-backend
```

Put a component in `tools` when the primary artifact is an executable:

```text
tools/package-inspect
tools/model-convert
tools/runtime-cli
```

## 5. When to Use Domains

Use `domains` when all of the following are true:

- The code is not generic core infrastructure.
- The code is more than a small plugin.
- The code may contain library code, plugins, tools, and tests.
- The code may eventually be installable as a grouped feature.
- The domain name matters to users or downstream integrators.

Do not use `domains` for every feature. If a feature is generic, place it in `lib`. If a feature is only a plugin, place it in `plugins`.

## 6. Public Headers

Keep public headers centralized:

```text
include/project/Core
include/project/Runtime
include/project/ComponentA
```

For domain-specific public API, choose one clear external name:

```text
include/domain-name/ComponentA
```

or:

```text
include/project/DomainName/ComponentA
```

Use the first form when the domain is a recognizable external product or ecosystem. Use the second form when the domain is mostly an internal extension of the main project.

Avoid scattering public headers under every `lib` or `domain` directory unless the project has a strong subproject/package model.

## 7. Naming Conventions

Recommended defaults:

| Object | Convention | Example |
|---|---|---|
| Top-level directories | lowercase | `include`, `plugins` |
| Generic component dirs | PascalCase | `Core`, `Runtime` |
| Domain dirs | kebab-case | `vendor-format` |
| Plugin dirs | lowercase capability names | `pitch`, `onnx`, `wav` |
| Tool dirs | executable names; use `-cli` for command-line tools | `model-convert-cli` |
| CMake aliases | lowercase namespace | `project::core` |
| Unit test targets | one project-wide pattern | `tst-core` or `core-tests` |

Do not mix naming rules accidentally. Mixed naming is acceptable only when each form has a documented meaning.

## 8. CMake Target Strategy

Separate real target names from user-facing aliases.

Real targets may preserve historical names during migration:

```cmake
add_library(old-core-name ...)
```

Public aliases should be regular and stable:

```cmake
add_library(project::core ALIAS old-core-name)
add_library(project::runtime ALIAS project-runtime)
```

For grouped optional domains, consider an interface target:

```cmake
add_library(project-domain INTERFACE)
target_link_libraries(project-domain INTERFACE project-domain-a project-domain-b)
add_library(project::domain ALIAS project-domain)
```

This allows downstream users to depend on the domain group without knowing every internal library.

## 9. CMake Folder Policy

IDE target folders should be broad categories, not a mirror of source paths:

```text
Libraries
Domains
Plugins
Tools
Tests
Examples
ThirdParty
Legacy
```

Avoid deep folders such as:

```text
Libraries/Product/Runtime/Core/Internal
Plugins/Product/Domain/PluginType/ConcretePlugin
Tests/Product/Module/Unit
```

The source tree already encodes structure. IDE folders should optimize target browsing.

## 10. Tests

Use `unittests` for tests that primarily validate one library or component:

```text
unittests/Core
unittests/Runtime
unittests/ComponentA
```

Use `tests` for behavior crossing multiple libraries or involving packaging/runtime behavior:

```text
tests/smoke
tests/integration
tests/e2e
```

A useful rule:

```text
unittests may know internals.
tests should behave like users.
```

## 11. Plugins

Prefer plugin paths that answer one of two questions:

```text
Which host loads this plugin?
Which domain owns this plugin?
```

Good examples:

```text
plugins/G2P/mandarin
plugins/Driver/onnx
plugins/diff-singer/pitch
```

Avoid adding a type layer unless the host has many plugin types and the type is operationally important:

```text
plugins/host/type/concrete-plugin
```

Flattening plugins usually improves navigation. Put shared support code in `lib/<Host>/Support`, not under `plugins/.../Utils`, when that support code is not itself a plugin.

## 12. Legacy Deletion

Legacy code should be deleted, not gradually blended into the new layout. If it cannot be removed in the same patch, keep it in a temporary deletion area that is not linked by production targets:

```text
legacy-delete/old-product
legacy-delete/old-api
```

Recommended policy:

- Do not install legacy headers or libraries.
- Do not link legacy targets from new production targets.
- Keep a named owner and deletion checkpoint for every temporary legacy path.
- Avoid public installation unless explicitly required.
- Do not modernize legacy APIs in the same patch as moving them.
- Document what new code must not depend on.

## 13. Migration Order

Recommended order for an existing project:

```text
1. Document the target layout and naming rules.
2. Simplify CMake target folders.
3. Add stable public aliases.
4. Move public headers.
5. Move generic libraries.
6. Move plugins and plugin support code.
7. Move tools.
8. Move unit and integration tests.
9. Delete or temporarily quarantine legacy code.
10. Rename namespaces or real targets only after layout stabilizes.
```

Avoid combining these in one patch:

| Combined change | Why it is risky |
|---|---|
| Directory move + namespace rename | Too much churn, hard review |
| Plugin move + plugin ID change | Runtime loading regressions |
| Header move + compatibility layer | Hidden long-term API surface |
| Keeping legacy linked + adding new APIs | New architecture inherits old assumptions |

## 14. Validation Checklist

Each migration phase should have a clear validation boundary:

```text
CMake configure passes
full build passes
unit tests pass
integration tests pass when affected
install tree is inspected when install rules change
exported target smoke test passes when CMake package rules change
```

Prefer small buildable phases. A clean final tree is less valuable than a sequence of changes that can be reviewed, built, and reverted independently.

## 15. Decision Heuristics

Use these questions when placing a new file:

| Question | If yes |
|---|---|
| Is it public API? | `include` |
| Is it generic implementation? | `lib/<Component>` |
| Is it domain-specific but first-class? | `domains/<domain>` |
| Is it dynamically loaded or registered as an extension? | `plugins/<host-or-domain>` |
| Is it an executable? | `tools/<name>` |
| Does it test one component? | `unittests/<Component>` |
| Does it test multiple components or installed behavior? | `tests/<kind>` |
| Is it old and still needed only as reference input? | `legacy-delete/<name>` with a deletion checkpoint |

The best layout is not the deepest or most descriptive one. It is the one where most files have an obvious home and new contributors can predict paths without reading CMake first.
