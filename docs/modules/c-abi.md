# C ABI 模块

namespace: (C API) | target: `synthrt::c` | 头文件: `include/synthrt/C/`

---

## 职责

C ABI 提供 synthrt 的 C 语言接口，供非 C++ 宿主（如 Python、Rust、C#）调用：
- `srt.h` — C ABI 公共头文件
- `srt_v4.cpp` — impl，内部组合 `VoicebankScanner` + `LanguageService` + `Runtime`
- `LastError.cpp/.h` — 线程本地错误缓冲 + `mapError()`

---

## 关键 API

### 句柄类型

```c
// srt.h
typedef struct srt_handle_t *srt_handle;   // 通用句柄基类
typedef struct srt_session_t *srt_session; // 会话句柄
```

### 生命周期

```c
// 进程级
srt_error srt_init(void);
srt_error srt_shutdown(void);

// 会话级
srt_session srt_session_create(void);
srt_error srt_session_destroy(srt_session session);
```

### 配置与刷新

```c
srt_error srt_session_set_package_paths(srt_session session,
                                        const char *const *paths, size_t count);
srt_error srt_session_set_plugin_paths(srt_session session,
                                       const char *const *paths, size_t count);
srt_error srt_session_refresh(srt_session session);
```

### 错误处理

```c
const char *srt_last_error(void);          // 线程本地错误字符串（toString 格式）
srt_error srt_last_error_code(void);       // 线程本地错误码（v4 新增）
void srt_clear_last_error(void);
void srt_free_string(char *str);
void srt_free_string_array(char **arr, size_t count);
```

`srt_last_error()` 返回 `[Category::Code] message\n  at file:line:function` 格式的完整错误描述（不仅是纯消息）。`srt_last_error_code()` 返回映射后的 `srt_error` 枚举值。

### setLastError 双通道错误码 (D-44)

`setLastError` 提供单参/双参两个重载：

```cpp
// lib/C/LastError.h
namespace srt::c::detail {
    void setLastError(std::string message);                              // 旧：code 固定 SRT_ERR_GENERIC
    void setLastError(std::string message, srt_error code);              // D-44：显式 code
    void setLastError(const srt::core::Error &error);                    // 从 Error 提取 toString + mapError
}
```

D-44 修复前，`srt_v4.cpp` 中 13 处调用单参数 `setLastError(message)`，TLS 错误码固定为 `SRT_ERR_GENERIC`，但函数实际返回 `SRT_ERR_INVALID_ARG` 或 `SRT_ERR_OUT_OF_MEM`，违反 ROBUST-05 双通道错误报告契约。修复后 13 处全部改为双参数：

- 10 处 `SRT_ERR_INVALID_ARG`（null session/handle、null paths/roots/phonemes 数组、null entry、null packageId/singerId）
- 3 处 `SRT_ERR_OUT_OF_MEM`（`new(std::nothrow)` 返回 nullptr 的 create 函数）

返回值与 TLS 错误码现在保证一致。

### 错误码

```c
typedef enum {
    SRT_OK = 0,
    SRT_ERR_INVALID_ARG,
    SRT_ERR_NOT_FOUND,
    SRT_ERR_INIT_FAILED,
    SRT_ERR_NOT_INIT,
    SRT_ERR_ALREADY_INIT,
    SRT_ERR_OUT_OF_MEM,
    SRT_ERR_FILE_IO,
    SRT_ERR_UNSUPPORTED,
    SRT_ERR_TIMEOUT,
    SRT_ERR_ABORTED,
    SRT_ERR_DEPENDENCY_CYCLE,
    SRT_ERR_LEVEL_MISMATCH,
    SRT_ERR_GENERIC,
    // vnext: appended handle-table error codes (ARCH-02: append-only within Level).
    SRT_ERR_INVALID_HANDLE, ///< Handle destroyed or never created.
    SRT_ERR_MODEL_BUSY,     ///< Model is busy with another task.
} srt_error;
```

vnext 在 `SRT_ERR_GENERIC` 后追加两个错误码（ARCH-02 append-only）：`SRT_ERR_INVALID_HANDLE`（句柄已 destroy 或从未创建）、`SRT_ERR_MODEL_BUSY`（同 model 已有运行中 task，协作式拒绝）。

---

## 内部实现

### SrtSession 结构

```cpp
// lib/C/srt_v4.cpp
struct SrtSession {
    ds::bank::VoicebankScanner scanner;
    ds::lang::LanguageService langSvc;
    srt::core::Runtime runtime;
};
```

每个 `srt_session` 持有独立的 `VoicebankScanner` + `LanguageService` + `Runtime` 组合，无全局单例。

### 异常边界隔离 (EX-06)

所有 `extern "C"` 函数包裹 try-catch，捕获 `std::exception` 并转为错误码 + LastError：

```cpp
extern "C" srt_error srt_session_refresh(srt_session session) {
    if (!session) {
        srt::c::detail::setLastError("srt_session_refresh: session handle is null");
        return SRT_ERR_INVALID_ARG;
    }
    try {
        auto result = toSession(session)->scanner.refresh();
        if (!result.hasValue()) {
            return srt::c::detail::mapError(result.takeError());
        }
        return SRT_OK;
    } catch (const std::exception &e) {
        srt::c::detail::setLastError(std::string("srt_session_refresh: ") + e.what());
        return SRT_ERR_GENERIC;
    }
}
```

### mapError 统一 (EX-07)

`mapError(srt::core::Error)` 在 `LastError.cpp` 中统一定义，`srt_v4.cpp` 不再有自己的 file-local 版本。v4 起 `mapError` 使用 `error.code()`（ErrorCode 枚举）映射，不再用 `error.type()`（int），解决了 G2P Error 类型冲突导致的误分类问题（BF-25）。`RecursiveDependency` 统一映射为 `SRT_ERR_DEPENDENCY_CYCLE`。

### setLastError 存储 toString (BF-29)

`setLastError(const Error &error)` 存储 `error.toString()` 而非 `error.message()`，C 调用方通过 `srt_last_error()` 可看到完整的 `[Category::Code] message\n  at file:line:function` 格式输出。

### srt_session_set_plugin_paths (BF-30)

`srt_session_set_plugin_paths` 返回 `SRT_ERR_UNSUPPORTED`（不再静默返回 `SRT_OK`），明确告知调用方此接口未实现。

---

## 调用关系

```
C 调用方 (Python/Rust/C#)
  │
  ├── srt_init()
  ├── srt_session_create()
  │     └── new SrtSession { scanner, langSvc, runtime }
  │
  ├── srt_session_set_package_paths(session, paths, count)
  │     └── session->scanner.setSearchPaths(paths)
  │
  ├── srt_session_refresh(session)
  │     └── session->scanner.refresh()
  │
  ├── srt_session_destroy(session)
  │     └── delete session (析构 scanner/langSvc/runtime)
  │
  └── srt_shutdown()
```

---

## 测试

`tests/abi/CAbiSkeletonTest.cpp` — C ABI 骨架测试，验证 `srt_session_create/destroy/set_*_paths/refresh` 可调用。

`unittests/C/test_c_abi.cpp` — C ABI 单元测试，覆盖 `srt_last_error`/`srt_last_error_code` 生命周期、错误码映射、BF-25/BF-29/BF-30 回归。

`unittests/C/test_c_abi_input_validation.cpp` — D-44 修复前因零大小数组（C2466）和按值实例化不透明类型（C2079）在 MSVC 上无法编译，掩盖了 3 个运行时测试失败。D-44 改用 `{"/tmp"}` + `count=0` 触发 INVALID_ARG 路径、`reinterpret_cast<...>(0xDEADBEEF)` 从整数构造指针值触发 INVALID_HANDLE 路径，恢复编译并暴露真实测试结果。修复后 `synthrt-unittest-c` 63 cases / 144 assertions 全通过。

---

## vnext: session/model/task 句柄 API (WP6 + WP7 + WP8)

参考 `docs/refactoring-vnext/04-diagnostics-degradation-and-migration.md` "最小 C ABI"。vnext 在原 ABI 之上追加（不替换）三种新句柄：`srt_SessionHandle`、`srt_ModelHandle`、`srt_TaskHandle`，分别绑定 `ds::session::VoicebankSession`、`ds::session::ModelSetHandle` 与 `shared_future<RefreshResult>`。WP6 又新增 `srt_RuntimeHandle` / `srt_LanguageServiceHandle` 两种资源句柄，支持 `srt_session_create_with_resources` 资源注入构造（D-27/K-01）。

### 句柄类型与任务状态

```c
// srt.h
typedef struct srt_SessionHandle srt_SessionHandle;
typedef struct srt_ModelHandle srt_ModelHandle;
typedef struct srt_TaskHandle srt_TaskHandle;

// v3 / WP6：借入式资源句柄。caller 拥有，session 通过 create_with_resources 借入。
typedef struct srt_RuntimeHandle srt_RuntimeHandle;
typedef struct srt_LanguageServiceHandle srt_LanguageServiceHandle;

typedef enum {
    SRT_TASK_STATE_PENDING = 0,
    SRT_TASK_STATE_RUNNING = 1,
    SRT_TASK_STATE_SUCCEEDED = 2,
    SRT_TASK_STATE_FAILED = 3,
    SRT_TASK_STATE_CANCELLED = 4,
} srt_TaskState;
```

### Session / Model / Task API（18 个 vnext 函数：14 WP7+WP8 + 4 WP6）

```c
// Session（WP7）
srt_SessionHandle *srt_session_create_v2(void);
srt_error          srt_session_destroy_v2(srt_SessionHandle *handle);
srt_error          srt_session_set_roots(srt_SessionHandle *handle,
                                         const char *const *roots, size_t count);
srt_error          srt_session_set_reserved_phonemes(srt_SessionHandle *handle,
                                                     const char *const *phonemes, size_t count);
srt_TaskHandle    *srt_session_refresh_async(srt_SessionHandle *handle);
const void        *srt_session_snapshot(srt_SessionHandle *handle);

// Session with resources（WP6 / D-27）
srt_SessionHandle *srt_session_create_with_resources(
    srt_RuntimeHandle *runtime,
    srt_LanguageServiceHandle *languageService);

// Resource handles（WP6）
srt_RuntimeHandle         *srt_runtime_create(void);
srt_error                  srt_runtime_destroy(srt_RuntimeHandle *handle);
srt_LanguageServiceHandle *srt_language_service_create(void);
srt_error                  srt_language_service_destroy(srt_LanguageServiceHandle *handle);

// Model（WP7）
srt_ModelHandle   *srt_model_create(srt_SessionHandle *session,
                                    const char *packageId,
                                    const char *singerId,
                                    const char *version);
srt_error          srt_model_destroy(srt_ModelHandle *handle);

// Task（WP7）
srt_TaskState      srt_task_state(srt_TaskHandle *handle);
srt_error          srt_task_wait(srt_TaskHandle *handle, int timeout_ms);
srt_error          srt_task_cancel(srt_TaskHandle *handle);
void               srt_task_destroy(srt_TaskHandle *handle);
const char        *srt_task_result_json(srt_TaskHandle *handle, size_t *out_size);

// Buffer（WP7）
void               srt_free_buffer(void *ptr);
```

### WP6: 资源注入式 session (D-27 / D-36)

`srt_session_create_with_resources(runtime, languageService)` 等价于 C++ 的 `VoicebankSession(SessionResources{...})`。两个 handle 必须非空且必须由调用方保活到 session 销毁之后：

- `runtime` / `languageService` 任一为 NULL → 返回 NULL，`srt_last_error` 设为 `InvalidArgument`。
- session 通过非拥有 aliasing `shared_ptr` 借用底层对象，不延长其生命周期。
- 默认构造的 `srt_session_create_v2()` 仅走 discovery 路径（D-36/K-10），调用 `convertG2p` / `createModelSet` 会返回 `G2pNotImplementedError` / `InferenceNotInitialized`。

资源句柄自己管理 `Runtime` / `LanguageService` 实例的生命周期（`create` / `destroy` 显式配对），可被多个 session 共享借用。`destroy` 是幂等的：销毁后同一指针值仍可传回库，decode 为缺失表项返回 `SRT_ERR_INVALID_HANDLE`（与 session/model/task 一致）。

### 函数语义与错误路径

| 函数 | 语义 | 成功返回 | 失败路径 |
|---|---|---|---|
| `srt_session_create_v2` | 构造 `SessionData{ session }` 并插入句柄表 | 非 NULL `srt_SessionHandle*` | NULL（OOM，见 `srt_last_error`） |
| `srt_session_destroy_v2` | 销毁句柄表项；运行中 task 通过 `shared_ptr` 保持 session 存活 | `SRT_OK` | `SRT_ERR_INVALID_HANDLE`（已 destroy 或 NULL） |
| `srt_session_set_roots` | 委托 `VoicebankSession::setRoots`（拷贝路径数组） | `SRT_OK` | `SRT_ERR_INVALID_HANDLE`、`SRT_ERR_INVALID_ARG`（paths 为 NULL 但 count>0） |
| `srt_session_set_reserved_phonemes` | 委托 `VoicebankSession::setReservedPhonemes` | `SRT_OK` | 同上 |
| `srt_session_refresh_async` | 调 `VoicebankSession::refreshAsync()` 取 `shared_future`，启动 detached watcher 线程，返回 task 句柄 | 非 NULL `srt_TaskHandle*` | NULL（invalid handle 或 future 已失效） |
| `srt_session_snapshot` | 返回 `VoicebankSession::snapshot().get()` 的原始指针 | 非 NULL（快照存在）/ NULL（无快照或 invalid handle） | 不报错 |
| `srt_model_create` | 委托 `VoicebankSession::createModelSet(SingerRef)`；成功则包装为 `ModelData` 插入表 | 非 NULL `srt_ModelHandle*` | NULL（invalid session、`createModelSet` 失败） |
| `srt_model_destroy` | 销毁 model 句柄；运行中 task 通过 `shared_ptr` 保持 handle 存活 | `SRT_OK` | `SRT_ERR_INVALID_HANDLE` |
| `srt_task_state` | 读取 `TaskData::state`（mutex 保护） | 当前状态 | invalid handle 返回 `SRT_TASK_STATE_FAILED` |
| `srt_task_wait` | 阻塞等待 watcher 线程终态或超时 | `SRT_OK`（终态）/ `SRT_ERR_TIMEOUT` | `SRT_ERR_INVALID_HANDLE` |
| `srt_task_cancel` | 设置 `TaskData::cancelRequested=true`；refresh 不可取消（仅作未来 G2P/S2P/Stage 用） | `SRT_OK` | `SRT_ERR_INVALID_HANDLE` |
| `srt_task_destroy` | 销毁 task 句柄（不中断 watcher 线程） | — | NULL 为 no-op |
| `srt_task_result_json` | 终态后返回 JSON 字符串（`RefreshResult` 经 `serializeRefreshResult` 序列化） | 新分配 buffer / NULL | 调用方需 `srt_free_buffer` |
| `srt_free_buffer` | 释放 `srt_task_result_json` 返回的 buffer | — | NULL 为 no-op |

### RefreshResult JSON 序列化

`serializeRefreshResult(const RefreshResult&)` 输出 UTF-8 JSON，结构如下：

```json
{
  "succeeded": true,
  "coalesced": false,
  "changed": true,
  "errorMessage": "",
  "snapshot": {
    "generation": 3,
    "roots": ["/path/a", "/path/b"],
    "reservedPhonemes": ["SP", "AP"],
    "availability": { "available": 2, "degraded": 0, "unavailable": 1 },
    "packages": [{ "packageId": "pkg", "version": "1.0.0", "valid": true }],
    "singers":  [{ "packageId": "pkg", "singerId": "s", "version": "1.0.0",
                   "resolutionState": "Resolved", "inferenceIds": ["duration"] }]
  },
  "changes": {
    "added":    [{ "packageId": "pkg", "version": "1.0.0" }],
    "removed":  [],
    "changed":  [],
    "disabled": []
  },
  "diagnostics": [{ "code": "PackageManifestInvalid", "message": "...", "packageId": "broken" }],
  "updatesAvailable": []
}
```

失败时 `succeeded=false`，`snapshot` 仍为旧快照（或初始空快照），`errorMessage` 说明原因。

### 内部实现

#### HandleTable 模板

```cpp
// lib/C/HandleTable.h
namespace srt::c_api;

using HandleId = uint64_t;
constexpr HandleId kInvalidHandle = 0;

template <typename T>
class HandleTable {
public:
    HandleId create(std::shared_ptr<T> obj);           // 失败返回 kInvalidHandle
    std::shared_ptr<T> lookup(HandleId id) const;      // 无效返回 nullptr
    bool destroy(HandleId id);                          // 销毁表项；shared_ptr 引用计数 -1
    void clear();                                       // shutdown 用
};
```

**关键设计**：
- 句柄 ID 从 1 开始递增；0 表示无效（NULL 指针）。
- C ABI 不透明指针直接 `reinterpret_cast<HandleId>`：destroy 后同一指针值仍可传回库，decode 为缺失表项返回 `SRT_ERR_INVALID_HANDLE`，不会 use-after-free（ROBUST-03）。
- 表项持有 `shared_ptr<T>`（强引用），保证 session/model 在 destroy 前存活。task 的 detached watcher 线程通过 `shared_ptr<TaskData>` 持有引用，destroy 不中断运行中 task（destroy race safety）。
- 表自身用 `std::mutex` 保护。

#### SessionData / ModelData / TaskData

```cpp
// lib/C/HandleTable.h
struct SessionData {
    ds::session::VoicebankSession session;  // 真实 session（线程安全）
};

struct ModelData {
    std::shared_ptr<SessionData> session;
    std::shared_ptr<ds::session::ModelSetHandle> handle;  // 绑定 snapshot generation
    std::string packageId, singerId, version;
    bool tryAcquire();  // ModelBusy 协作（CAS）
    void release();
};

struct TaskData {
    std::shared_ptr<SessionData> session;
    enum class Type { Refresh, G2p, S2p, Stage };
    Type type = Type::Refresh;
    std::shared_future<ds::session::RefreshResult> refreshFuture;
    // watcher 线程更新 state/resultJson/errorMessage/errorCode
    mutable std::mutex mutex;
    std::condition_variable cv;
    srt_TaskState state = SRT_TASK_STATE_PENDING;
    std::string resultJson, errorMessage;
    int errorCode = 0;
    bool cancelRequested = false;
};
```

#### Watcher 线程模式

`runRefreshWatcher(shared_ptr<TaskData>)` 在 detached 线程中执行：
1. 阻塞 `refreshFuture.get()`（不超时，与 C++ session 的 `shared_future` 语义一致）
2. 终态后加锁 `TaskData::mutex`，根据 `RefreshResult.succeeded` 设置 `state=SUCCEEDED/FAILED`、`errorMessage`、`errorCode`
3. 调用 `serializeRefreshResult` 写入 `resultJson`
4. `cv.notify_all()` 唤醒 `srt_task_wait` 调用方

不使用 `std::async`：`std::async` 返回的 future 析构会阻塞，破坏 detached 语义。watcher 线程通过 `shared_ptr<TaskData>` 自持生命周期，task destroy 不中断 watcher。

#### ModelBusy 协作

`ModelData::tryAcquire()` 用 `std::atomic_bool` CAS：同 model 句柄上的并发 `srt_model_*` 调用，第二个起返回 `SRT_ERR_MODEL_BUSY`。`release()` 在调用方函数返回前调用（RAII guard 模式）。C++ 同步 API 不产生 `ModelBusy`，仅 C ABI 异步路径需要。

#### 异常边界

所有 vnext `extern "C"` 函数包裹 try-catch（CODING-02）：
- `std::exception` → `setLastError(e.what())` + `SRT_ERR_GENERIC`
- `...` → `setLastError("unknown")` + `SRT_ERR_GENERIC`
- NULL 句柄前置检查返回 `SRT_ERR_INVALID_HANDLE` 不进入 try

### destroy 竞态安全性

| 场景 | 行为 |
|---|---|
| `destroy(session)` 后调用 `srt_session_*` | 表项缺失，返回 `SRT_ERR_INVALID_HANDLE` |
| `destroy(session)` 时仍有运行中 refresh task | task 的 watcher 线程持有 `shared_ptr<TaskData>` → `shared_ptr<SessionData>`，session 存活至 task 终态 |
| `destroy(model)` 时仍有运行中 stage task | task 持有 `shared_ptr<ModelData>` → `shared_ptr<ModelSetHandle>`，handle 存活至 task 终态；`start()` 已返回的 inference 仍可 `stop()` |
| `destroy(task)` 后调用 `srt_task_*` | 返回 `SRT_ERR_INVALID_HANDLE`；watcher 线程不受影响 |
| 同一 model 句柄并发调用 | `tryAcquire` 失败返回 `SRT_ERR_MODEL_BUSY` |

### 测试

`tests/abi/SessionHandleTest.cpp` — WP7 + WP8 测试：
- 6 个 destroy race 单测（WP7）：destroy→InvalidHandle、destroy 不中断运行中 task、协作取消、线程安全、ModelBusy 协作、错误码映射
- 3 个端到端单测（WP8）：真实 voicebank 刷新、model create 失败路径（无 Runtime→`InferenceNotInitialized`、空快照→失败）、invalid handle 稳定性
