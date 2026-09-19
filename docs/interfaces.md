# 公共接口 v1

这是三人协作的起始契约。字段含义、状态转换与退出码的变更必须同 PR 更新此文档。

## 核心 C++ 模型 v2（worker JSON / IPC 仍为 v1）

定义：`include/cps/core/scheduler.hpp`；纯 C++17，不依赖 Qt。每次领取仍为非抢占式。

`Job` 原字段 `id/title/pages/copies/submittedMs/state` 保留。新增：

- `priority`：0–100，越大越优先；`maxAttempts`：1–100，默认 3，包含首次执行。
- `attempts`：每次 claim 加 1，作为本次执行令牌；所有重试相关回调必须携带原令牌。
- `queuedSinceMs`：本轮进入队列的时间；`accumulatedWaitMs`：已结束的排队区间总时长。
- `firstStartedMs/finishedMs`：可空；`lastChangedMs`：最后一次状态变更。
- `runs`：每次执行的 `startedMs/stoppedMs`，未结束的 stoppedMs 为空。

时间为同一时间轴上的整数毫秒，范围 0–2^53−1。默认 `nowMs()` 使用构造时 UTC 毫秒锚点加 steady_clock 的经过时间；GUI 提交和结束使用同一个时钟。回放直接传入模拟时间，不读取真实时钟。注入的 Clock 必须线程安全；显式状态变更不能早于该任务上次变更。B 重启恢复时必须沿用持久时间轴，处理系统时钟倒退，不能把不同进程的原始 steady_clock 值直接持久化比较。

### 排序

- FCFS：本轮入队时间升序。
- SJF：`pages * copies` 升序，再按本轮入队时间。
- PriorityAging：`priority + floor((accumulatedWaitMs + now - queuedSinceMs) / agingIntervalMs)` 降序。默认间隔 30,000 ms，可通过 SchedulerOptions 配置；累计等待保留到重试，不包含执行时间。有效分数不封顶。同分按本轮入队时间，再保持初次插入顺序。
- 所有策略只领取已到达且处于 Queued 的任务；完全并列保持插入顺序。重试进入本轮队尾，首次 submittedMs 不变。

### 操作与状态

- `submit(Job)`：只接收没有运行历史的新任务，校验参数和重复 ID；排队时间初始化为 submittedMs。
- `claim(policy, now)`：在一个锁内完成选取、Queued → Running、令牌更新和计时；无可领取任务返回空。`claim(policy)` 使用注入时钟。
- `finish(id, attempt, success, now)`：Running → Completed / Failed；拒绝旧令牌和重复结束。
- `cancel(id, now)`：Queued → Cancelled（立即生效）；Running → Cancelling（只表达取消意图）。
- `acknowledgeCancel(id, attempt, now)`：Cancelling → Cancelled。**B 确认执行已停止后**调用。取消与最终输出成功竞争时，B 确认输出已提交可调用 finish(success=true)，让成功生效。
- `retry(id, attempt, now)`：B 确认本轮失败并停止后调用；Running → Queued，达到 maxAttempts 则直接 Failed。取消中的任务不得重新排队。
- `snapshot()`：返回独立副本，包括所有历史计时字段；不向 UI 暴露内部引用。
- `restore(jobs, now)`：仅向空调度器加载完整、无重复 ID、时间与状态一致的快照，失败不修改调度器。恢复顺序保持数组顺序；调用者同时恢复相同的 SchedulerOptions。不会自动认定旧 Running 已停止，也不会自行重跑；由 B 对账后调用上述操作。快照中的事件时间不能晚于 now。

旧 `finish(id, success)` 仅允许首次领取的任务，保留现有 GUI 的兼容性；存在重新领取时必须用显式令牌 API。这里的 attempt 是协调层领取次数，不能和 supervisor 内部 worker 重启次数混用。

B 负责快照序列化、原子持久化、进程停止与输出对账；此版本只提供内存状态与加载接口。跨进程恢复后若只能知道“现在确认进程已死”，run 的结束时间是保守估计，不能宣称精确物理打印时长。C 负责取消按钮、优先级控件及统计展示。

## 性能统计接口

`statistics(snapshot, windowStart, windowEnd, printers)` 位于 `include/cps/metrics/statistics.hpp`，输入必须是调度器产生的有效快照，描述 windowEnd 时刻。返回纯数据 Statistics，C 可直接转成显示模型。越界窗口或不足以容纳历史并发的打印机数抛出 invalid_argument。

- submitted、firstStarted、completed、failed、cancelled：相应事件发生在闭区间 [start, end] 内的数量；相邻窗口汇总时注意边界事件不能重复相加。
- averageFirstWaitMs / maxFirstWaitMs：窗口内首次开始的任务，从 submitted 到 firstStarted 的等待，不因后来失败或取消排除它们。
- averageTotalQueueWaitMs：窗口内成功任务的所有排队区间总和的均值，包含重试等待；averageTurnaroundMs 为其 finished − submitted 的均值。失败和取消不算成功样本。
- queued / running / cancelling：快照结束时的状态数量；maxCurrentQueueWaitMs 为仍在排队的任务的累计等待最大值。
- busyPrinterMs：每个 runs 区间与 [start, end) 相交的毫秒总和，未结束 run 截到 end。包括失败尝试和停止确认前的取消中时间，表示执行槽位占用。
- throughputPerSecond = completed × 1000 / 窗口毫秒；utilization = busyPrinterMs /（打印机数 × 窗口毫秒）。零长度窗口的速率和利用率为 0；空样本的均值为 0，必须结合样本数解读。

`cps::replay(workload, policy, ReplayOptions)` 位于 `include/cps/core/replay.hpp`。输入为新任务，不允许夹带执行历史或重复 ID。默认 2 台、150 ms/页、30,000 ms 老化间隔，按事件推进模拟时间；返回最终 Job 快照、派发记录与全程统计。细节及命令行见 `docs/replay.md`。

## Job JSON

文件 UTF-8、最大 64 KiB，写入使用原子替换。

```json
{"schema_version":1,"job_id":"demo-001","title":"课设文档","pages":8,"copies":1}
```

- job_id：1–80 位 ASCII 字母、数字、`_`、`-`；不能包含路径分隔符。
- title：去空白后非空、最多 200 个 Qt UTF-16 code units，用作展示文本，不作文件名。
- pages：整数 1–1000；copies：整数 1–100；总页数不超过 10,000。
- schema_version 必须为数值 1。未知字段目前忽略，未来不兼容变更升版本。
- v1 文件没有 submittedMs；GUI 排队时间保留在内存模型。后续持久队列必须补独立存储 schema，不能假定 worker 文件可恢复完整队列。

## 进程通信

`print-supervisor --job <file> --output-dir <dir>` 启动同目录 `print-worker`。
stdout 为 UTF-8 JSON Lines，一行一个对象，分隔符统一为 LF；Windows CRT stdout 配置成二进制模式，避免转发时 CRLF 被二次转换。stderr 为诊断文本。接收方应缓存未完成的一行，不能假设一次 readyRead 对应一个对象。

所有事件包含 `schema_version: 1`、`type`、`job_id`。

| type | 字段 | 含义 |
| --- | --- | --- |
| worker_started | pid、attempt（1 开始） | supervisor 成功启动真实子进程 |
| progress | printed_sheets、total_sheets | 本次尝试的进度，重试可回到 0 |
| completed | output_path | worker 完成原子输出 |
| worker_timeout | 无 | 监督者准备结束静默子进程 |
| worker_restarting | restart、exit_code | 已决定退避后重试 |
| supervisor_completed | restarts | 整个受监督任务成功 |
| failed | reason 或 exit_code，可能有 restarts | 整个受监督任务失败 |

只以正常退出 0 加最终输出为任务成功依据，不因进度到 100% 就先记完成。worker 的 completed 在原子 commit 成功之后发出。
监督事件另外追加到 `<job_id>.events.jsonl`；当前日志未记录全部 worker 进度。

## 文件与锁

- `<job_id>.txt`：模拟最终结果，失败重试使用同名原子替换，不追加重复页。
- `.<job_id>.supervisor.lock`：同一输出目录中同一任务只能有一个 supervisor。
- `.<job_id>.output.lock`：同一任务只能有一个 writer，即使直接运行 worker 也受约束。
- 使用 QLockFile，禁用按锁年龄自动失效；依靠进程存活判断处理崩溃遗留锁。
- 当前约定仅针对本机文件系统；不声称网络共享文件系统的锁和原子写保证相同。

## 退出码

| 代码 | 含义 |
| --- | --- |
| 0 | 正常完成 |
| 64 | 参数或 Job 无效 |
| 69 | 平台不支持 daemon / daemon 初始化失败 |
| 70 | supervisor 的重试耗尽 |
| 73 | 无法创建目录、日志或输出文件 |
| 74 | 写入或原子提交失败 |
| 75 | 任务文件锁已被占用 |
| 78 | 找不到或无法启动 worker |

Windows 的 TerminateProcess 与 POSIX 信号的退出值不同，测试只依赖成功/失败语义，不硬编码崩溃的原始退出码。
