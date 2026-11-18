# Task 执行与取消语义

本文记录 `srt::ITask` 的统一执行协议。wolf 的 `ExecutiveTask`、otter 的 `AnalysisTaskBase` 和 dsinfer 的 Session/Task 实现都使用这一协议，不再各自维护 claim、generation、callback 和等待状态。

## 公开控制面

宿主只使用以下控制方法：

```cpp
Expected<std::unique_ptr<TaskResult>> start(const TaskStartInput &input);
Expected<void> startAsync(std::shared_ptr<const TaskStartInput> input, AsyncCallback callback);
Expected<void> stop();
Expected<void> waitForFinished();
```

`start` 和 `startAsync` 共用一个执行 claim。一个 claim 覆盖 body 和异步 callback。body 或 callback 尚未结束时，另一线程的新执行请求以 `srt::Error::InvalidArgument` 拒绝。

`stop()` 只请求停止已经存在的 claim。空闲调用成功但没有效果，不会预取消下一次执行，也不会修改最近一次结果留下的状态。

`waitForFinished()` 等待 body 和异步 callback 都结束。它不销毁 Task。从同一个 callback 内调用时立即返回，因此 callback 可以安全销毁 Task 或 Executive。

## 实现钩子

派生 Task 实现：

```cpp
Expected<std::unique_ptr<TaskResult>> execute(const TaskStartInput &input) override;
```

`execute()` 在 claim 已建立后调用，可以抛异常。公共控制方法、状态更新、worker 创建、callback 交付和等待都由基类完成。

需要检查输入身份的 Task 覆写 `validateInput()`。输入在取得 claim 之前检查，失败不改变状态。

拥有模型 Session 的 Task 可以覆写 protected 的 `quit()` 和 `wait()`。`stop()` 调用 `quit()` 发出停止请求，`waitForFinished()` 在公共 body/callback 屏障之后调用 `wait()` 等待额外资源。

## Callback

callback 在完成 body 的 worker 上交付。callback 可以：

- 启动下一次执行。
- 销毁 Task 或 Executive。
- 抛异常。

从 callback 启动下一次执行时，旧 worker 把 claim 和 generation 交给新执行。旧 worker 在 callback 返回后不会释放新执行的 claim。

callback 抛异常时统一捕获并记录 critical。异常不会逃出 detached worker，也不会让 claim 永久占用。callback 返回后，下一次执行可以正常开始。

## 状态

`State` 描述本次最终 outcome：

- 正常结果通常为 `Succeeded`。
- 错误通常为 `Failed`。
- 契约能够识别的取消可以为 `Canceled`。

最终状态的分类通过 `classifyOutcome()` 完成。synthrt 默认把 `srt::Error::Canceled` 映射为 `Canceled`。wolf 在返回部分结果并观察到 stop 时给出 `Canceled`。otter 在错误属于 `AnalysisError::Cancelled` 时给出 `Canceled`。

状态不由“本次执行期间是否收到过 stop”单独决定。body 在 stop 到达前已经返回其他错误或成功结果时，该 outcome 保持不变。

## 错误域

公共执行内核提供三个错误映射钩子：

- `exceptionError()`：body 抛出的标准异常。
- `unknownExceptionError()`：body 抛出的未知异常。
- `workerStartError()`：异步 worker 创建失败。

synthrt 默认使用 `srt::Error::Internal` 和系统错误码。wolf 把 worker 创建失败映射为 `wolf::ErrorCode::NoWorker`。otter 把 body 异常映射为 `AnalysisError::Internal`，把 worker 创建失败映射为 `AnalysisError::NoWorker`。

这样公共内核不依赖任何下游错误枚举，各库仍保留自己的可比较错误身份。

## 析构

Task 的具体析构函数在读取其成员的 body 可能仍运行时，先调用 `stop()` 再调用 `waitForFinished()`。`ITask` 析构自身只执行公共等待屏障，不能依赖已经销毁的派生虚函数。

Executive 的析构使用 protected 的 `quit()` 和 `wait()` 处理自己的 Task。`ContribExecutive` 基类析构随后递归停止、等待并删除子 Executive。公开词汇只保留 `stop`、`waitForFinished`、`quit` 和 `wait`，不使用 `finish`、`shutdown` 或其他同义生命周期入口。
