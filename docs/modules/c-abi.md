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
const char *srt_last_error(void);      // 线程本地错误字符串
void srt_clear_last_error(void);
void srt_free_string(char *str);
void srt_free_string_array(char **arr, size_t count);
```

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
} srt_error;
```

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

`mapError(srt::core::Error)` 在 `LastError.cpp` 中统一定义，`srt_v4.cpp` 不再有自己的 file-local 版本。`RecursiveDependency` 统一映射为 `SRT_ERR_DEPENDENCY_CYCLE`。

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

`unittests/C/test_last_error.cpp` — LastError 机制单元测试。
