# 代码规范化任务

日期: 2026-07-26
依据: [docs/design/design-guidelines.md](file:///d:/projects/synthrt/docs/design/design-guidelines.md) CODING-01..06
范围: 仅本项目（synthrt）生产代码与 module docs；不动公共头 `_` 前缀成员（CS-03 冻结）

---

## N1: 修正 module docs 中失效的 `ds::lang::` 引用

### 现状

[design-guidelines.md#L115](file:///d:/projects/synthrt/docs/design/design-guidelines.md) CODING-01 namespace 列表注明：

> `ds::lang` 为 forwarding shim，待删除，见 03-conventions.md §2.2 Q4

经源码核查（`grep -r "ds::lang" include/ lib/ domains/ plugins/` 无命中），`ds::lang` namespace 在源码中**已不存在**，forwarding shim 已在历史重构中删除。但 3 个 module docs 仍引用该 namespace，导致文档与代码不一致：

| 文件 | 行号 | 失效引用 |
|---|---|---|
| [docs/modules/overview.md#L121](file:///d:/projects/synthrt/docs/modules/overview.md) | 121 | `ds::lang::LanguageService langSvc;`（§3.2 组件式调用示例） |
| [docs/modules/g2p.md#L142](file:///d:/projects/synthrt/docs/modules/g2p.md) | 142 | `namespace ds::lang;`（API 文档 namespace 声明） |
| [docs/modules/c-abi.md#L116](file:///d:/projects/synthrt/docs/modules/c-abi.md) | 116 | `ds::lang::LanguageService langSvc;`（C ABI 用法示例） |

### 修复方案

将上述 3 处 `ds::lang::` 替换为 `srt::g2p::`（与 [LanguageService.h](file:///d:/projects/synthrt/include/synthrt/G2P/LanguageService.h) 实际 namespace 一致），并同步更新 [design-guidelines.md#L115](file:///d:/projects/synthrt/docs/design/design-guidelines.md) CODING-01 中的 namespace 列表，删除 `ds::lang` 条目（标注为"已删除"）。

### 设计准则核对

- CODING-01：namespace 统一为 `srt::g2p`，符合准则
- ARCH-02：仅修改文档，不动公共头签名
- INFRA-02：不留失效的 forwarding shim 引用

### 验证

- `grep -r "ds::lang" docs/` 应仅在历史勘误记录中出现（如有），不再作为 API 引用
- 不影响编译与测试

### 任务执行清单

- [ ] 替换 [docs/modules/overview.md](file:///d:/projects/synthrt/docs/modules/overview.md) §3.2 line 121
- [ ] 替换 [docs/modules/g2p.md](file:///d:/projects/synthrt/docs/modules/g2p.md) line 142
- [ ] 替换 [docs/modules/c-abi.md](file:///d:/projects/synthrt/docs/modules/c-abi.md) line 116
- [ ] 更新 [docs/design/design-guidelines.md](file:///d:/projects/synthrt/docs/design/design-guidelines.md) CODING-01 namespace 列表
- [ ] 单次提交：`refactor(N1): drop stale ds::lang references in module docs`

---

## N2: 修正生产代码 `.string()` 路径处理违反 CODING-03

### 现状

[design-guidelines.md#L127](file:///d:/projects/synthrt/docs/design/design-guidelines.md) CODING-03：

> 使用 `dsfw::PathUtils`（或 `stdc::path::to_utf8`）处理路径，不在日志/错误消息中直接 `path.string()`。

`std::filesystem::path::string()` 在 Windows 上返回系统代码页编码字符串，非 UTF-8；中文/Unicode 路径在日志中显示为乱码或丢失。

**生产代码违反（1 处文件，2 处行）**：

| 文件 | 行号 | 当前代码 | 问题 |
|---|---|---|---|
| [plugins/Extract/game/GameExtractor.cpp#L104](file:///d:/projects/synthrt/plugins/Extract/game/GameExtractor.cpp) | 104 | `"Could not open config.json: " + configPath.string()` | 中文路径显示乱码 |
| [plugins/Extract/game/GameExtractor.cpp#L139](file:///d:/projects/synthrt/plugins/Extract/game/GameExtractor.cpp) | 139 | `" (model dir: " + modelPath.string() + ")"` | 中文路径显示乱码 |

**测试代码违反（4 处文件，6 处行）**：

| 文件 | 行号 |
|---|---|
| [unittests/Audio/tst_audio_edge.cpp](file:///d:/projects/synthrt/unittests/Audio/tst_audio_edge.cpp) | 166, 187, 220, 252 |
| [unittests/Driver/tst_onnx_driver_reentry.cpp](file:///d:/projects/synthrt/unittests/Driver/tst_onnx_driver_reentry.cpp) | 417 |
| [tests/abi/tst_session_handle.cpp](file:///d:/projects/synthrt/tests/abi/tst_session_handle.cpp) | 259, 303 |

### 修复方案

**生产代码（优先级中）**：

`GameExtractor.cpp` 的 2 处 `path.string()` 替换为 `stdc::path::to_utf8(path)`，需在文件顶部 `#include <stdcorelib/path.h>`（若未包含）。

**测试代码（优先级低）**：

测试代码使用 `.string()` 多为传给接受 `std::string` 的 API（如 `decoder.open()`、`decoder.probe()`），或在测试场景中路径为 ASCII（如 `temp_directory_path()`）。CODING-03 主要约束日志/错误消息；测试代码：
- 若用于错误消息：同生产代码替换为 `stdc::path::to_utf8`
- 若用于 API 入参：保留 `.string()` 但加注释说明（避免误判为违反 CODING-03）
- 若用于纯 ASCII 测试 fixture：可保留不动

**本轮范围**：仅修复生产代码 `GameExtractor.cpp` 2 处。测试代码 6 处列入 [04-test-coverage.md](file:///d:/projects/synthrt/docs/refactor/04-test-coverage.md) T2 评估。

### 设计准则核对

- CODING-03：路径处理使用 `stdc::path::to_utf8` ✓
- ROBUST-05：错误消息含可读路径，便于排查 ✓
- 不引入新依赖（`stdc::path::to_utf8` 已被 [VoicebankSession.cpp#L115](file:///d:/projects/synthrt/domains/ds-session/lib/VoicebankSession.cpp) 使用）

### 验证

- 编译 `srt-extract-game` target 通过
- 用中文路径声库包冒烟（GameExtractor 加载 `config.json` 失败时错误消息正确显示中文）

### 任务执行清单

- [ ] 修复 [plugins/Extract/game/GameExtractor.cpp#L104](file:///d:/projects/synthrt/plugins/Extract/game/GameExtractor.cpp)
- [ ] 修复 [plugins/Extract/game/GameExtractor.cpp#L139](file:///d:/projects/synthrt/plugins/Extract/game/GameExtractor.cpp)
- [ ] 确认 `#include <stdcorelib/path.h>` 已存在或新增
- [ ] 单次提交：`refactor(N2): use stdc::path::to_utf8 for error messages in GameExtractor`

---

## N3: 处理仓库根 squash 工件

### 现状

仓库根目录有 2 个未跟踪文件，为 v6 阶段 squash 57 个未推送 commit 时保留的辅助脚本与列表：

| 文件 | 用途 | 当前状态 |
|---|---|---|
| `_commits_to_squash.txt` | squash 输入清单（57 个 commit hash + 标题） | untracked |
| `_squash.ps1` | squash 执行脚本（PowerShell） | untracked |

依据 [project_memory](file:///c:/Users/99662/.trae-cn/memory/projects/-d-projects-synthrt/project_memory.md)（2026-07-26 02:14:42）："A backup branch (backup-refactor-before-squash) 保留 at 3c684bd. _squash.ps1 和 _commits_to_squash.txt 保留 as documentation."

### 修复方案

3 种处理选择（执行时择一）：

1. **归档到 docs**（推荐）：移至 `docs/refactor/archive/squash-artifacts/`，并加 `.gitignore` 排除仓库根同名文件。保留可追溯性，又不污染仓库根。
2. **删除**：squash 已完成且 backup branch `backup-refactor-before-squash` 保留在 `3c684bd`，可彻底删除工件文件。
3. **加 `.gitignore`**：仅排除，不移动。文件保留在本地工作区但不再出现在 `git status`。

### 设计准则核对

- INFRA-01：仓库根目录仅放顶层构建/项目文件；归档文件应进 `docs/` 子目录
- INFRA-02：squash 工件非公共 API，无发布契约

### 任务执行清单

- [ ] 选择处理方案（默认方案 1：归档）
- [ ] 若归档：`mkdir docs/refactor/archive/squash-artifacts && git mv _commits_to_squash.txt _squash.ps1 docs/refactor/archive/squash-artifacts/`
- [ ] 在 `docs/refactor/archive/squash-artifacts/README.md` 简述文件来源与保留理由
- [ ] 单次提交：`chore(N3): archive squash artifacts under docs/refactor/archive/`

---

## 修订记录

| 日期 | 版本 | 说明 |
|---|---|---|
| 2026-07-26 | v1 | 初稿：识别 N1/N2/N3 三项规范化任务 |
