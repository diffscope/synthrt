# Task 执行与取消语义

本文记录 `srt::ITask` 的统一执行协议。`TaskExecution` 保存一次执行的 claim、worker、callback 和等待状态，`TaskHandle` 是宿主观察这次执行的句柄，`TaskStopToken` 把取消观察从 Task 对象中分离。wolf 的 `ExecutiveTask`、otter 的 `AnalysisTaskBase` 和 dsinfer 的 Session/Task 实现都使用这一协议。

## 公开控制面

宿主只使用以下控制方法：

```cpp
Expected<std::unique_ptr<TaskResult>> start(const TaskStartInput &input);
Expected<TaskHandle> startAsync(std::shared_ptr<const TaskStartInput> input,
                                AsyncCallback callback);
Expected<void> stop();
Expected<void> waitForFinished();
```

`start` 和 `startAsync` 共用一个当前执行槽位。`startAsync` 返回的 `TaskHandle` 拥有本次执行的共享状态。body 或 callback 尚未结束时，另一线程的新执行请求以 `srt::Error::InvalidArgument` 拒绝。

`stop()` 只请求停止已经存在的 claim。空闲调用成功但没有效果，不会预取消下一次执行，也不会修改最近一次结果留下的状态。

`waitForFinished()` 等待 body 和异步 callback 都结束。它不销毁 Task。从同一个 callback 内调用时立即返回，因此 callback 可以安全销毁 Task 或 Executive。

## 实现钩子

派生 Task 实现：

```cpp
Expected<std::unique_ptr<TaskResult>>
    execute(const TaskStartInput &input, const TaskStopToken &token) override;
```

`execute()` 在 claim 已建立后调用，可以抛异常。公共控制方法、状态更新、worker 创建、callback 交付和等待都由基类完成。

需要检查输入身份的 Task 把输入的 `ContribContract` 传给 `ITask` 的 protected 构造函数。`ITask` 在取得 claim 之前比较契约，失败不改变状态。

拥有模型 Session 的 Task 可以覆写 protected 的 `quit()` 和 `wait()`。`TaskHandle::stop()` 先请求本次执行的 `TaskStopSource`，再调用 `quit()`。`TaskHandle::wait()` 在公共 body/callback 屏障之后调用 `wait()` 等待额外资源。

`ITask::stop()`、`waitForFinished()` 和 `state()` 是便捷入口，转发到当前 `TaskHandle`。需要明确持有某次异步执行时，调用者保存 `startAsync()` 返回的 handle。

## Callback

callback 在完成 body 的 worker 上交付。callback 可以：

- 启动下一次执行。
- 销毁 Task 或 Executive。
- 抛异常。

从 callback 启动下一次执行时，Task 为下一次执行创建新的 `TaskExecution` 和 `TaskHandle`。旧执行只释放自己的状态，不会覆盖新执行。

callback 抛异常时统一捕获并记录 critical。异常不会逃出 detached worker，也不会让 claim 永久占用。callback 返回后，下一次执行可以正常开始。

## 状态

`State` 描述本次最终 outcome：

- 正常结果通常为 `Succeeded`。
- 错误通常为 `Failed`。
- 契约能够识别的取消可以为 `Canceled`。

最终状态的分类通过 `classifyOutcome(outcome, stopRequested)` 完成。synthrt 默认把 `srt::Error::Canceled` 映射为 `Canceled`。wolf 在返回部分结果并观察到 stop 时给出 `Canceled`。otter 在错误属于 `AnalysisError::Cancelled` 时给出 `Canceled`。

状态不由“本次执行期间是否收到过 stop”单独决定。body 在 stop 到达前已经返回其他错误或成功结果时，该 outcome 保持不变。

## 错误域

公共内核把未捕获的 body 异常映射为 `srt::Error::Internal`。需要自己的错误域的 typed Task 在 `execute()` 中捕获 body 异常并返回该域的错误；otter 因而仍返回 `AnalysisError::Internal`。

`startWorker()` 返回 `Expected<void>`。默认实现创建 detached thread，并把 `std::system_error` 转换为 synthrt 错误。wolf 和 otter 覆写这个钩子，把失败转换为各自的 `NoWorker`。公共内核因此不依赖任何下游错误枚举或错误映射枚举。

## 析构

Task 的具体析构函数在读取其成员的 body 可能仍运行时，先调用 `stop()` 再调用 `waitForFinished()`。`ITask` 析构自身只等待当前执行的 body/callback 屏障，不能依赖已经销毁的派生虚函数。析构随后从执行内核 detach Task 的虚函数钩子。callback 中销毁 Task 时，外部保留的 `TaskHandle` 仍可等待执行结束。`stop()` 仍可请求该执行自己的 `TaskStopSource`，但不会再调用已经销毁的 `quit()`。

Executive 的析构使用 protected 的 `quit()` 和 `wait()` 处理自己的 Task。`ContribExecutive` 基类析构随后递归停止、等待并删除子 Executive。公开词汇只保留 `stop`、`waitForFinished`、`quit` 和 `wait`，不使用 `finish`、`shutdown` 或其他同义生命周期入口。
