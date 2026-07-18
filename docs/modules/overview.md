# SynthRT 模块调用文档总览

日期: 2026-07-18

定位: 本文档提供 synthrt 各模块的调用关系总览。各模块详细 API 见同目录下的独立文档。文档详略得当：核心调用路径详述，边缘场景略述。

---

## 1. 模块清单

| 模块 | namespace | CMake target | 头文件根 | 文档 |
|---|---|---|---|---|
| Core | `srt::core` | `synthrt::core` | `include/synthrt/Core/` | [core.md](core.md) |
| Driver | `srt::driver` | `synthrt::driver` | `include/synthrt/Driver/` | [driver.md](driver.md) |
| SVS | `srt::svs` | `synthrt::svs` | `include/synthrt/SVS/` | [svs.md](svs.md) |
| G2P | `srt::g2p` / `ds::lang` | `synthrt::g2p` | `include/synthrt/G2P/` | [g2p.md](g2p.md) |
| S2P | `srt::s2p` | `synthrt::s2p` | `include/synthrt/S2P/` | [s2p.md](s2p.md) |
| DS Bank | `ds::bank` | `srt-ds::bank` | `include/diffsinger/Bank/` | [ds-bank.md](ds-bank.md) |
| DS Infer | `ds::infer` | `srt-ds::infer` | `include/diffsinger/Infer/` | [ds-infer.md](ds-infer.md) |
| DS Session | `ds::session` | `srt-ds::session` | `include/diffsinger/Session/` | [ds-session.md](ds-session.md) |
| C ABI | (C API) | `synthrt::c` | `include/synthrt/C/` | [c-abi.md](c-abi.md) |

---

## 2. 顶层调用关系

```
宿主层 (ds-editor-lite SynthrtEngine / dsinfer-cli)
  │
  └── ds::session::VoicebankSession     声库会话（唯一入口）
        ├── ds::bank::VoicebankScanner       声库扫描（委托）
        ├── srt::g2p::LanguageService        G2P/S2P 转换（委托）
        ├── srt::core::Runtime               模型加载（委托）
        │     └── srt::driver::setupOnnxInferenceDriver  ONNX 驱动注册
        ├── ds::infer::SingerStageResolver   stage 解析（委托）
        ├── ds::infer::ModelSet              惰性加载/卸载（通过 ModelSetHandle）
        │     └── srt::svs::Inference        推理任务
        └── ds::infer::InferenceService      全流水线便利封装 (CLI/批量)
```

---

## 3. 典型调用流程

### 3.1 会话式调用（vnext 推荐，宿主层唯一入口）

```cpp
#include <diffsinger/Session/VoicebankSession.h>

// 1. 创建 session 并注入依赖（D-27/K-01：session 借入 Runtime + LanguageService，
//    不拥有其生命周期；调用方需先 initialize()/loadPackage()）
ds::session::VoicebankSession session(
    ds::session::SessionResources{
        .runtime = &runtime,
        .languageService = langSvc,        // shared_ptr<LanguageService>
    });
session.setRoots(voicebankPaths);
session.setReservedPhonemes({"SP", "AP"});

// 2. 刷新声库。Lite worker 走同步 refresh()（D-29/K-03）；CLI/tests/C ABI 用 refreshAsync()
auto result = session.refresh();
if (!result.succeeded) {
    // result.snapshot 仍是旧快照（或空），result.errorMessage 说明原因
    return;
}
auto snap = result.snapshot;             // shared_ptr<const VoicebankSnapshot>

// 3. 查询能力（三态 Availability + 结构化诊断）
ds::bank::SingerRef ref("pkg", "singer", "1.0.0");
auto summary = session.capabilitySummary(ref);
if (summary.availability != ds::session::Availability::Ready) {
    // 查 summary.diagnostics 看降级原因
}

// 4. G2P / S2P 转换（V3-10：通过 version-aware LanguageService 路由；C++ 同步 Expected）
auto g2p = session.convertG2p(ref, "cmn", inputs);
auto s2p = session.convertS2p(ref, "cmn", "ni3");

// 5. 最终音素校验（S2P 后、+ note 分配、手工修改后调用）
auto valid = session.validatePhonemes(ref, finalPhonemes);
if (!valid) return;  // G2pValidationError：阻断下游推理

// 6. 创建 ModelSet 并推理（绑定当前 snapshot generation）
auto modelExp = session.createModelSet(ref);
auto handle = *modelExp;
handle->load(ds::infer::StageKind::Duration);
handle->start(ds::infer::StageKind::Duration, input);
auto taskResult = handle->result(ds::infer::StageKind::Duration);

// 7. 内容改变的刷新成功后旧 handle 变 stale：start() 返回 StaleModelSet，
//    load/stop/unload 仍允许（已运行任务可结束）。Lite 仅对未开始 task
//    丢弃旧 handle、重新 createModelSet、重试一次。
```

详见 [ds-session.md](ds-session.md)。

### 3.2 组件式调用（lite 风格，直接编排各模块）

```cpp
// 1. 初始化 Runtime + ONNX 驱动
srt::core::Runtime runtime;
srt::driver::OnnxDriverConfig config;
config.ep = srt::driver::onnx::DMLExecutionProvider;
srt::driver::setupOnnxInferenceDriver(runtime, pluginRoot, config);

// 2. 扫描声库
ds::bank::VoicebankScanner scanner;
scanner.setSearchPaths(voicebankPaths);
scanner.refresh();

// 3. 初始化语言服务（V3-01 version-aware：传入 PackageDirectoryEntry 向量）
ds::lang::LanguageService langSvc;
std::vector<srt::g2p::PackageDirectoryEntry> pkgDirs;
for (const auto &e : scanner.packageDirectories(packageId)) {
    pkgDirs.push_back({packageId, e.version, e.path});
}
langSvc.initializeMetadata(g2pPluginPaths, officialG2pPaths, pkgDirs);
langSvc.initializeModels();

// 4. 加载声库包
runtime.loadPackage(scanner.packageDirectories(packageId).front().path);

// 5. 解析 stage
ds::infer::SingerStageResolver resolver;
auto stageSetExp = resolver.resolve(runtime, packageId, singerId, version);

// 6. 构造 ModelSet（惰性，不创建模型）
ds::infer::ModelSet modelSet(std::move(*stageSetExp));

// 7. 按需加载 + 推理
auto loadExp = modelSet.load(ds::infer::StageKind::Duration);
auto &durationModel = modelSet.model(ds::infer::StageKind::Duration);
durationModel->start(input);

// 8. 独立卸载（如释放 vocoder 显存）
modelSet.unload(ds::infer::StageKind::Vocoder);

// 9. 全量释放
modelSet.unloadAll();
```

---

## 4. 模块依赖关系

```
srt::core          ← 基础设施（无下游依赖）
  ↑
srt::driver        ← 依赖 core
  ↑
srt::svs           ← 依赖 core（InferenceSpec/InferenceCategory）
  ↑
srt::g2p           ← 依赖 core + s2p（G2P 任务内部调用 S2P）
  ↑
srt::s2p           ← 依赖 core
  ↑
ds::bank           ← 依赖 core（声库扫描，无推理依赖）
ds::infer          ← 依赖 core + svs + driver（推理编排）
ds::session        ← 依赖 core + g2p + ds-bank + ds-infer（会话编排层）
srt::c             ← 依赖 core + g2p + ds-bank + ds-infer（C ABI 组合层）
```

---

## 5. 插件体系

| 插件类型 | 目录 | 注册类别 | 说明 |
|---|---|---|---|
| ONNX Driver | `plugins/Driver/onnx/` | `inference` / `dsdriver` | ONNX Runtime 推理后端 |
| G2P 插件 | `plugins/G2P/` | G2P Manager | chain/lstm/mandarin/cantonese/ds-dict |
| DiffSinger 推理 | `plugins/diffsinger/` | `inference` / `singer` | acoustic/duration/pitch/variance/vocoder + singer-provider |

插件通过 `Runtime::loadPackage()` 加载声库包时由 `InferenceCategory`/`SingerCategory` 解析 spec，推理时由 `InferenceSpec::createInference()` 创建实例。

---

## 6. 测试体系

| 层级 | 目录 | 框架 | 说明 |
|---|---|---|---|
| 单元测试 | `unittests/` | Catch2 v3 | 单组件，不加载插件。包含 `test_error_system.cpp`（ErrorCode/toString/工厂函数）、`test_c_abi.cpp`（C ABI + BF-25/BF-29/BF-30 回归）、`test_g2p_error_migration.cpp`（G2P Error 迁移） |
| 领域测试 | `domains/*/unittests/` | Catch2 v3 | ds-bank/ds-infer 领域内。包含 `test_modelset_errors.cpp`、`test_singer_resolver_ambiguity.cpp`、`test_speaker_mapper.cpp` 等 |
| 跨模块测试 | `tests/` | Catch2 v3 | smoke/integration/abi/packaging。`tests/abi/SessionHandleTest.cpp` 覆盖 vnext WP7+WP8 的 session/model/task 句柄 destroy race 与端到端 |
| CLI 测试 | `tools/dsinfer-cli/` | 手动 | `--test-lite-style` lite 风格流水线 |

GitHub Actions CI (`.github/workflows/build.yml`) 在 Windows/Linux/macOS 三平台执行编译 + `ctest` 测试，ONNX Runtime 使用缓存，vcpkg overlay ports 通过 git submodule 引入。vcpkg binary caching（x-gha 后端）缓存 FFmpeg 等编译产物避免重复构建；FFmpeg 仅启用 avcodec/avformat/swresample 三特性。
