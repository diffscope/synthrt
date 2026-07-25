# Contributing to synthrt

This document is the entry point for contributors. It is a **thin pointer** to
[`docs/design/design-guidelines.md`](docs/design/design-guidelines.md),
which is the binding specification for every PR during the `refactor/vnext`
effort. New and modified files must follow that document in full; when this
file and `03-conventions.md` disagree, the latter wins.

## 1. Project structure

The repository layout, library/plugin/test directory conventions, and the
internal static sub-library rules are defined in
[03-conventions.md §1](docs/design/design-guidelines.md#1-目录布局规范).

Key invariants:

- Public headers live under the repository-root `include/synthrt/` or
  `include/diffsinger/` trees only. Local `include/synthrt/` inside a library
  is forbidden (fixes S-01).
- `.cpp` files live under `src/<Component>/`; library roots must not contain
  loose source files (fixes S-02).
- `internal/` is reserved for **plugin** implementations; libraries use
  `src/` (fixes PS-01).
- Internal `STATIC` sub-libraries follow the `support/<sublib>/` layout and
  the `NO_INSTALL` / lowercase namespace rules in
  [§1.5](docs/design/design-guidelines.md#15-内部-static-子库目录布局新增).

## 2. Build environment

CMake presets are defined in [`CMakePresets.json`](CMakePresets.json)
(version 3, requires CMake 3.21+; see
[03-conventions.md §9](docs/design/design-guidelines.md#9-cmakepresetsjson-规范)).
Dependencies are managed via vcpkg; the toolchain file is picked up from
`$env:VCPKG_ROOT`.

Quick start (Windows, Ninja):

```powershell
cmake --preset windows-debug
cmake --build build/windows-debug
ctest --test-dir build/windows-debug --preset windows-debug
```

Toolchain and compiler minimums are in
[03-conventions.md §14](docs/design/design-guidelines.md#14-编译器最低版本要求)
(C++20; MSVC 19.29+, GCC 10.3+, Clang 14+, Apple Clang 13.1+).

## 3. Coding conventions

The authoritative rules live in
[03-conventions.md §2–§4](docs/design/design-guidelines.md#2-namespace-规范):

- **Namespace** ([§2](docs/design/design-guidelines.md#2-namespace-规范)):
  `namespace ↔ directory ↔ target` one-to-one. Only `srt::` (libraries) and
  `srt-ds::` (domains) aliases are kept; the legacy `synthrt::` double alias
  is removed (NS-03). Namespace closing comments are required
  (`} // namespace srt::core`).
- **Naming** ([§3](docs/design/design-guidelines.md#3-命名规范)):
  PascalCase for classes/files, camelCase for functions/locals, `m_` prefix
  for member variables, `_impl` for the PIMPL pointer, `tst_*.cpp` for tests.
- **Includes** ([§4](docs/design/design-guidelines.md#4-include-规范)):
  cross-module public headers use `<synthrt/...>` / `<diffsinger/...>`;
  same-directory private headers use `"ClassName_p.h"`. Include order is
  maintained by `clang-format` via `IncludeBlocks: Regroup`.

The `.clang-format` configuration is
[§6](docs/design/design-guidelines.md#6-clangformat-配置规范); run
`clang-format -i` before committing. Note the special handling of
`extern "C" {}` include blocks (§6.2 rule 3).

## 4. CMake conventions

All `add_library` / `add_executable` must go through the `synthrt_add_*`
macro family (`synthrt_add_library`, `synthrt_add_executable`,
`synthrt_add_plugin`) plus `synthrt_declare_target` and `synthrt_install`.
The only exceptions are `IMPORTED` and `INTERFACE` aggregate targets. See
[03-conventions.md §5](docs/design/design-guidelines.md#5-cmake-规范).

Source globbing is unified to
`file(GLOB_RECURSE _src CONFIGURE_DEPENDS "src/*.cpp")` (§5.3 rule 10), and
the qmsetup integration (`qm_import`, `qm_configure_target`, `qm_basic_install`,
…) must be preserved (§5.3 rule 4).

CMake files are formatted by `cmake-format` using
[`.cmake-format.json`](.cmake-format.json) (T-P3-01).

## 5. PIMPL conventions

PIMPL private headers are named `<ClassName>_p.h` and placed in
`src/<Component>/` next to the `.cpp`. The `Impl` pointer is
`std::unique_ptr<Impl> _impl;` and every `Impl` member uses the `m_` prefix.
Chain-PIMPL inheritance must be documented inline. See
[03-conventions.md §10](docs/design/design-guidelines.md#10-pimpl-规范).

## 6. Commit conventions

Follow [Conventional Commits](https://www.conventionalcommits.org/), as
specified in
[03-conventions.md §12](docs/design/design-guidelines.md#12-提交规范):

```
<type>(<scope>): <subject>

<body>

<footer>
```

Allowed `type`: `feat`, `fix`, `refactor`, `style`, `docs`, `test`,
`chore`, `ci`.

`scope` is the module name (`core`, `g2p`, `ds-bank`, `ds-infer`,
`ds-session`, `audio`, `s2p`, `svs`, `extract`, `c`, `driver`, `build`,
`docs`).

Comments and Doxygen blocks must be in English (Q9, see
[§11](docs/design/design-guidelines.md#11-注释语言规范)); migration
history comments belong in git log, not in source.

## 7. Pull request flow

- Branch: `refactor/vnext` is the integration branch for the refactoring
  effort.
- Commit granularity: **one task, one commit** (see the task list in
  [`04-implementation-plan.md`](docs/refactoring-vnext/04-implementation-plan.md)).
  Do not bundle multiple T-P* tasks into a single commit.
- Use explicit `git add <file>` rather than `git add -A` to avoid staging
  unintended files.
- Do not push force, and do not push directly to `main`/`master`.
- Do not mix vcpkg baseline bumps with refactoring commits (§18.3 rule 4).

## 8. CI matrix

CI must pass all 7 cells defined in
[03-conventions.md §18.1](docs/design/design-guidelines.md#181-ci-矩阵定义)
(Windows MSVC Debug/Release, Ubuntu GCC Debug/Release, Ubuntu Clang Debug,
macOS Intel Debug, macOS Apple Silicon Release). Any failing cell blocks the
PR (§18.4 rule 1).

Tool versions are pinned (§18.2):

| Tool | Version | Hook rev |
|---|---|---|
| clang-format | LLVM 16 | `v16.0.6` |
| clang-tidy | LLVM 16 | `v16.0.6` |
| cmake-format | 0.6.13 | `v0.6.13` |
| pre-commit | 3.6.0+ | — |
| CMake | 3.21+ (3.25+ recommended) | — |
| Ninja | 1.11+ | — |

The `rev` fields in [`.pre-commit-config.yaml`](.pre-commit-config.yaml) must
be concrete tags; `main`/`latest` are forbidden (§18.4 rule 3).

## 9. Static analysis baseline

- **clang-tidy** runs in **warn-only** mode during the refactoring phase
  (T-P0-08 baseline). New code must pass clang-tidy; legacy code is cleaned up
  per module. Promotion to fail-on is tracked by T-P3-04. See
  [03-conventions.md §7](docs/design/design-guidelines.md#7-clangtidy-配置规范).
- **clang-format** is enforced via the pre-commit hook (T-P3-05).
- **Include cycle detection** is provided by
  [`scripts/check-include-cycles.py`](scripts/check-include-cycles.py).
- **cmake-format** is enforced via `.cmake-format.json` (T-P3-01).
