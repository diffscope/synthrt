# ONNX Runtime 接入 onnxruntime-builds 端口

分支 `onnxruntime-builds-uptake`，基于 `main`。§1 至 §9 为方案与依据，§10 为实施结果。

## 1. 目标

**ORT 的版本与 synthrt 的版本相互独立。**

接入前二者并不独立：`scripts/setup-onnxruntime.cmake` 固定了 `_version_ort "1.17.3"` 与整套 SHA512，而生态中实际使用的端口版本是 1.24.4。更改 ORT 版本必须修改 synthrt 仓库，本方案要消除的正是这一耦合。

## 2. ORT 的归属

| 场景 | ORT 提供方 | 依据 |
| :-- | :-- | :-- |
| 被编辑器调用 | 编辑器 | ORT 与 synthrt 在编辑器侧是两个独立的 port，synthrt 既无法也不应代为管理 |
| synthrt 独立构建与测试 | synthrt 的**测试部分** | 测试是 synthrt 自身的应用程序，应当自行决定使用哪份 ORT |
| 库与插件本身 | 无 | 库与插件只需要编译期的头文件 |

由此得到全文的主结论：

> **synthrt 只使用 ORT 的头文件，不部署 ORT 的运行库。**

插件 CMake 中将 ORT 复制到 `runtimes/onnx/<flavor>` 的逻辑随之整体删除。

## 3. 接入前核实的事实

| # | 事实 | 出处 |
| :-- | :-- | :-- |
| 1 | 端口本体无需改动。refactor 内嵌 overlay、`scripts/vcpkg` 子模块、ds-editor-lite 三处的 onnxruntime-builds 端口逐文件字节相同，均为 1.24.4 port-version 8 | 三处 `vcpkg.json` 与 `portfile.cmake` 对比 |
| 2 | ORT 版本由 synthrt 自身固定 | `scripts/setup-onnxruntime.cmake` |
| 3 | **驱动从不在 `runtimes/onnx/` 中查找运行库**。`DriverInitArgs::runtimePath` 是宿主传入的目录，为空时退回系统加载器的搜索 | `OnnxDriverApi.h`、`OnnxDriver.cpp` |
| 4 | 上述复制逻辑的**唯一实际使用者是本仓库的测试** | `test_InferenceDriverFactory.cpp` 拼出 `bundlePath / "runtimes" / "onnx" / "default"` |
| 5 | 头文件可以完全由 imported target 传递，因为 qmsetup 的 `LINKS`（裸名）是 PUBLIC | `QMSetupAPI.cmake` |
| 6 | ORT 头文件不暴露给 dsinfer 的客户端，前向声明隔离了这些头文件 | `OnnxDriverApi.h` |
| 7 | `third-party/CMakeLists.txt` 的内容仅为 `# Empty`，该目录中只有此文件和一个忽略 `/onnxruntime` 的 `.gitignore` | 目录清点 |

第 3 与第 4 条是本方案成立的前提：复制运行库不是驱动运行的必要条件，只为测试提供便利。

## 4. synthrt 侧的变更

### 4.1 `dsinfer/util/onnxutil/CMakeLists.txt`：全仓库唯一引用 ORT 的位置

```cmake
find_package(onnxruntime-builds CONFIG QUIET)

if(NOT TARGET onnxruntime-builds::default)
    message(WARNING "ONNX Runtime package (onnxruntime-builds) not found, so the ONNX driver "
                    "plugin will not be built. The vcpkg \"onnx\" feature installs the package.")
    return()
endif()

dsinfer_add_library(${PROJECT_NAME} STATIC NO_INSTALL
    SOURCES ${_src}
    LINKS dsinfer onnxruntime-builds::default
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}
    DEFINES ORT_API_MANUAL_INIT
)
```

头文件目录随 imported target 传递，不写路径。`LINKS` 为 PUBLIC，因此下游目标可以获得该目录。

### 4.2 `dsinfer/plugins/inferencedrivers/onnxdriver/CMakeLists.txt`

删除 ORT 目录变量、按平台区分的 glob、两次 `qm_add_copy_command` 与 CUDA 目录探测块。保留源文件、EP 宏、`ORT_API_MANUAL_INIT` 与 `LINKS_PRIVATE ... onnxutil`。插件以 PRIVATE 方式链接 `onnxutil` 即可获得自身编译所需的 usage requirements，此文件不含任何 ORT 路径。

### 4.3 `dsinfer/tests/auto/Inference/CMakeLists.txt`：由测试选择 ORT

在已有的 `if(TARGET test_InferenceDriverFactory AND TARGET onnxdriver)` 块中增加：

```cmake
find_package(onnxruntime-builds CONFIG QUIET)
target_compile_definitions(test_InferenceDriverFactory PRIVATE
    DSINFER_TEST_DRIVER_PLUGIN_PATH="$<TARGET_FILE_DIR:onnxdriver>/.."
    DSINFER_TEST_ORT_RUNTIME_DIR="${ONNXRUNTIME_BUILDS_RUNTIME_DIR}"
)
```

### 4.4 `test_InferenceDriverFactory.cpp`

```diff
-   initArgs.runtimePath = bundlePath / "runtimes" / "onnx" / "default";
+   initArgs.runtimePath = DSINFER_TEST_ORT_RUNTIME_DIR;
```

共两处，均为测试代码而非插件代码。`runtimes/onnx/default` 字符串同时从插件 CMake 与测试代码中移除。

### 4.5 `scripts/vcpkg-manifest/vcpkg.json`：可选 feature

```json
"onnx": {
    "description": "Build the ONNX inference driver and the tests that exercise it",
    "dependencies": [ "onnxruntime-builds" ]
}
```

结构与既有的 `tests` feature 相同。不启用该 feature 时不下载载荷，也不构建驱动。启用后才能独立运行驱动测试。

### 4.6 移除 ORT 版本的最后来源

- 删除 `scripts/setup-onnxruntime.cmake`
- 删除 `third-party/`（含内容为 `# Empty` 的 CMakeLists 与 `.gitignore`）
- 删除顶层 `CMakeLists.txt` 中的 `add_subdirectory(third-party)`
- README 中的三条 `cmake -E chdir third-party ...` 命令替换为说明：ORT 由 overlay 的 `onnxruntime-builds` 端口提供，通过 `--x-feature=onnx` 启用

完成后，`grep -ri "1\.17\.3\|third-party/onnxruntime\|runtimes/onnx" .` 在本仓库中除本文档外应无匹配。

## 5. 消费侧的变更

依赖 synthrt main 分支的端口原先手动将 ORT 头文件复制到 `third-party/onnxruntime/default/include`，唯一原因是 main 分支会探测该目录。该目录不再被探测后，复制步骤随之删除。端口的 `onnx` feature 依赖 `onnxruntime-builds`，二者安装在同一棵 installed 树中，因此 `find_package` 可以直接找到该包。

## 6. 不在范围内的改动

| 事项 | 理由 |
| :-- | :-- |
| 修改端口 | 端口归 ds-editor-lite 维护，且现有接口已满足需要 |
| 新增 `onnxdriver-payload.cmake` | 该文件的前提是 synthrt 部署载荷且宿主需要镜像其布局。synthrt 不再部署载荷，因此没有需要声明的对象 |
| 导出 CUDA flavor | synthrt 不再部署载荷，flavor 由应用选择。插件的 `DSINFER_ENABLE_CUDA` 与 `DSINFER_ENABLE_DIRECTML` 编译宏保持不变 |
| 修改插件 C++ | 本方案改动的 C++ 仅为测试中的两行 |
| 缺少端口时报错终止 | 保持 warning 加 `return()`，与原有行为一致 |
| 为 `find_package` 指定版本下限 | 与独立控制 ORT 版本的目标相悖。ORT 的 C API 向后兼容，且插件通过 `ORT_API_MANUAL_INIT` 在运行期获取 API，编译期只需要头文件 |

### 端口侧的可选改进（由 lite 决定）

若修改端口，还可以省去两处 CMake 代码。一是将 `ORT_API_MANUAL_INIT` 放入 `INTERFACE_COMPILE_DEFINITIONS`，消费者无需再定义该宏。二是为运行库导出真正的 `IMPORTED SHARED` 目标，宿主即可使用 CMake 自带的 `$<TARGET_RUNTIME_DLLS>` 与 `install(IMPORTED_RUNTIME_ARTIFACTS)`，两侧的 glob 均可删除。这是减少 CMake 代码的最终形态，但该改动属于端口而非本仓库。

## 7. 验收判据

1. `grep -ri "1\.17\.3\|third-party/onnxruntime\|runtimes/onnx" .` 在本仓库中除本文档外无匹配。
2. 启用 `--x-feature=onnx` 构建：构建出 onnx 驱动，`test_InferenceDriverFactory` 通过，且其 `runtimePath` 指向端口目录而非插件所在目录。
3. 不启用该 feature 构建：不构建驱动，跳过驱动测试，其余测试全部通过。
4. 消费侧端口删除复制步骤后，启用 onnx feature 安装成功，消费侧测试全部通过（包括运行真实模型的用例）。
5. **解耦的直接检验**：将 overlay 子模块切换到 ORT 版本不同的提交后，synthrt 与消费侧无需修改任何文件即可构建。

## 8. 实施顺序

1. synthrt：§4.1 至 §4.4，每步单独构建（此时 onnx 驱动仍应能构建并通过测试）
2. synthrt：§4.5 manifest，§4.6 清理，验证判据 1 至 3
3. 消费侧：按 §5 删除复制步骤，验证判据 4
4. 判据 5

## 9. 遗留事项

lite 侧读取 `SYNTHRT_ONNXDRIVER_FLAVORS` 的逻辑应随部署职责一并迁移，不在本轮范围内。main 分支不提供该声明，这一点需要告知 lite 维护者。

## 10. 实施结果

### 10.1 与方案的两处偏差

方案的变更清单未涉及 `dsinfer` 的构建接线，实际实施多出两处改动，二者均修复**既有缺陷**，而非本次引入的问题：

| 新增改动 | 必要性 |
| :-- | :-- |
| `dsinfer/tools/cli/CMakeLists.txt`：`add_dependencies(... onnxdriver)` 改为条件式 | `SYNTHRT_BUILD_DSINFER` **默认为 ON**，因此未安装 ORT 时直接执行 `cmake -B build` 是默认路径，而驱动目标缺失时该依赖在 generate 阶段失败 |
| `dsinfer/tests/auto/Inference/CMakeLists.txt`：驱动测试目标改为条件式 | 该测试包含 `OnnxTensor.h`，而该头文件只经由条件块中的 `onnxutil` 提供，因此启用测试且缺少 ORT 时无法编译 |

设计理由：本改动**承诺缺少端口时可以降级构建**，而原有构建树无法兑现该承诺，不修复则降级路径实际不可用。两处改动都只增加了插件目标是否存在的判断，没有引入新机制。

### 10.2 验证结果

| 配置 | 结果 |
| :-- | :-- |
| 缺少端口，默认配置（dsinfer ON） | 配置与构建通过 |
| 缺少端口，启用 dsinfer 与 tests | 配置与构建通过，驱动用例按条件省略，其余测试全部通过 |
| 安装端口，启用 dsinfer 与 tests | 全部通过。驱动测试输出 `Initialized ONNX Runtime 1.24.4`，二进制文件中写入的路径是端口的 `share/onnxruntime-builds/runtime/default` |
| 插件目录旁的 `runtimes/` | 不存在 |
| 消费侧完整构建与最小构建 | 全部通过 |

判据 5（解耦）通过静态检查核实：synthrt 与消费侧均不包含 ORT 版本号，唯一来源是端口的 `setup-onnxruntime/versions.cmake`。本次未使用不同版本的载荷实际运行，因为这需要一个带不同版本的 overlay 提交与一次真实下载。

### 10.3 消费侧的联合测试

消费侧新增一个模拟编辑器实际部署方式的用例：将 ORT 载荷部署到**宿主自选的临时目录**（既不是端口目录，也不是插件所在目录），再运行完整链路，并通过 `DriverExtension::runtimePath` 断言驱动从该目录加载。将期望值替换为端口目录时该用例失败，因此该断言有效。

原有用例都将端口目录直接传给驱动，虽然也由应用选择目录，但所选目录恰好是端口所在位置。新用例覆盖了应用将载荷移至其他位置再告知 synthrt 的情形，这是唯一能暴露对端口位置或插件相邻目录隐含依赖的情形。