# 公共接口 v1

这是三人协作的接口契约。字段含义、状态转换与退出码的变更必须在同一提交中更新此文档。

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
- `restore(jobs, now)`：仅向空调度器加载完整、无重复 ID、时间与状态一致的快照，失败不修改调度器。本轮入队时间必须与运行历史吻合，Running 的最后变更时间必须等于本轮开始时间，损坏值直接拒绝。恢复顺序保持数组顺序；调用者同时恢复相同的 SchedulerOptions。不会自动认定旧 Running 已停止，也不会自行重跑；由 B 对账后调用上述操作。快照中的事件时间不能晚于 now。

旧 `finish(id, success)` 仅允许首次领取的任务，保留现有 GUI 的兼容性；存在重新领取时必须用显式令牌 API。这里的 attempt 是协调层领取次数，不能和 supervisor 内部 worker 重启次数混用。

B 使用 `state_store` 完成快照序列化与原子持久化，并由 `print-coordinator` 负责进程停止、输出对账和恢复。跨进程恢复后若只能知道“现在确认进程已死”，run 的结束时间是保守估计，不能宣称精确物理打印时长。C 负责取消按钮、优先级控件及统计展示。

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
- v1 worker 文件没有 submittedMs，也不包含状态历史；不能用它恢复完整队列。完整 Scheduler 快照由 coordinator 的独立持久状态保存。

## 持久化协调状态 v1

`include/cps/runtime/state_store.hpp` 定义 `PersistentState`。coordinator 将它以 UTF-8 JSON 原子写入 `<state-dir>/state.json`，最大读取 64 MiB。

顶层字段：`schema_version: 1`、`kind: campus-print-state`、`saved_at_ms`、`policy`、`printers`、`aging_interval_ms`、`jobs`。每个 job 保存核心模型的全部恢复字段，包括 state、priority、maxAttempts、attempts、queued/accumulated wait、first/finished/lastChanged 和完整 runs。

读取时先校验 JSON、范围、job_id 和结构，再调用 `Scheduler::restore()` 做状态机一致性校验。服务重启后的时间轴以 `max(当前 UTC, saved_at_ms)` 为新锚点，再叠加本进程 steady_clock 经过时间，避免系统时间倒退造成显式事件时间回退。

## Coordinator 本地 IPC v1

`print-coordinator` 使用 `QLocalServer`；客户端使用 `QLocalSocket`。传输仍为 UTF-8 JSON Lines，每行一个完整 JSON 对象。请求必须含 `schema_version: 1`、非空 `request_id`、`type`；响应回显 `request_id`。单连接缓冲上限当前为 1 MiB。

| 请求 type | 主要字段 | 作用 |
| --- | --- | --- |
| submit | `job`：job_id/title/pages/copies，可选 priority/max_attempts | 持久提交新任务 |
| snapshot | 无 | 返回 policy、printers 与完整 jobs 快照 |
| cancel | job_id | Queued 立即取消；Running 持久化取消意图并通知 supervisor |
| set_policy | policy=`fcfs|sjf|priority` | 改变后续领取策略并持久化 |
| stats | 可选 window_start_ms/window_end_ms（整数，须在 [0, 当前时间] 内） | 调用 A 的 statistics() 返回窗口内统计 DTO；缺省窗口为 [0, now] |
| shutdown | 无 | 停止接收新连接，等待活动/恢复中的任务收尾后退出 |

异步广播使用同一连接发送。`job_state` 携带完整 job 快照；supervisor/worker 的 versioned 事件也会原样转发。C 必须按行分帧，并用 `request_id` 区分请求响应与无 request_id 的异步事件。

## 进程通信

`print-supervisor --job <file> --output-dir <dir>` 启动同目录 `print-worker`。coordinator 还会传入 attempt 唯一的 `--cancel-file <file>`；supervisor 轮询该 marker，收到取消后停止 worker 且不再重启。
stdout 为 UTF-8 JSON Lines，一行一个对象，分隔符统一为 LF；Windows CRT stdout 配置成二进制模式，避免转发时 CRLF 被二次转换。stderr 为诊断文本。接收方应缓存未完成的一行，不能假设一次 readyRead 对应一个对象。

所有事件包含 `schema_version: 1`、`type`、`job_id`。

| type | 字段 | 含义 |
| --- | --- | --- |
| worker_started | pid、attempt（1 开始） | supervisor 成功启动真实子进程 |
| progress | printed_sheets、total_sheets | 本次尝试的进度，重试可回到 0 |
| completed | output_path | worker 完成原子输出 |
| worker_timeout | 无 | 监督者准备结束静默子进程 |
| worker_restarting | restart、exit_code | 已决定退避后重试 |
| cancelled | 无 | supervisor 已停止本任务执行链 |
| supervisor_completed | restarts | 整个受监督任务成功 |
| failed | reason 或 exit_code，可能有 restarts | 整个受监督任务失败 |
| control_ready | port、claim_attempt | 可选回环控制端点已启动 |
| cancelling | claim_attempt | supervisor 已观察到取消（取消 marker 或 GUI 控制命令），正在停止 worker |
| cancelled | claim_attempt、reason | worker 已停止，本次尝试未提交输出 |
| fault_injected | claim_attempt、worker_attempt | 演示模式结束了实际 worker |

只以正常退出 0 加最终输出为任务成功依据，不因进度到 100% 就先记完成。worker 的 completed 在原子 commit 成功之后发出。
监督事件另外追加到 `<job_id>.events.jsonl`；当前日志未记录全部 worker 进度。

可选控制协议、断线重连、心跳租约及取消/提交对账见 [control-protocol.md](control-protocol.md)。
Job JSON 版本仍为 1；新 stdout 事件是可选控制模式的增量扩展，接收方需更新事件类型校验。
supervisor 启动的 worker 在原子输出首行写入本次 execution_token，允许取消竞争时识别本次已提交结果。

## 文件与锁

- `<job_id>.txt`：模拟最终结果，失败重试使用同名原子替换，不追加重复页。
- `.<job_id>.supervisor.lock`：同一输出目录中同一任务只能有一个 supervisor。
- `.<job_id>.output.lock`：同一任务只能有一个 writer，即使直接运行 worker 也受约束。
- `<state-dir>/.coordinator.lock`：同一状态目录只允许一个 coordinator。
- `<state-dir>/state.json`：完整 Scheduler/策略/打印机数快照，使用 QSaveFile 原子替换。
- `<state-dir>/control/<job_id>.<attempt>.cancel`：Running 取消的持久意图；attempt 防止旧取消误伤后续重试。
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
| 75 | 任务文件锁或 coordinator 状态目录锁已被占用 |
| 78 | 找不到或无法启动 worker |
| 130 | 已确认取消（正常退出；必须同时有 cancelled 事件） |

Windows 的 TerminateProcess 与 POSIX 信号的退出值不同，测试只依赖成功/失败语义，不硬编码崩溃的原始退出码。


## GUI/coordinator 统一协议

GUI 使用状态目录派生的 `QLocalServer` 名称（规范路径的 SHA-256 前 16 位）。Windows 路径规范后忽略大小写。
每条命令为一行 JSON，必含 `schema_version: 1`、非空 `request_id`、`type`。回复回传 request_id；异步事件不代替命令确认。
修改操作先持久化再回复 accepted，丢失确认时客户端不自动重发。命令帧上限 8 MiB，GUI 回复帧上限 64 MiB。

| type | 参数 | 语义 |
| --- | --- | --- |
| snapshot | 无 | 获取完整队列、策略、暂停状态、设备槽位、页数进度、后台时间及统计 |
| stats | 可选 window_start_ms/window_end_ms | 查询指定时间窗口的统计；缺省为 0/后台当前时刻，GUI 快照另外采用最早提交时刻作为起点 |
| submit | job 对象 | job_id/title/pages/copies，priority/max_attempts 可选 |
| submit_batch | jobs 数组 | 全部校验后原子入队，一次持久化；空批、重复 ID、非法字段或超限拒绝整个批次 |
| set_policy | policy: fcfs/sjf/priority | 只影响后续领取，正在执行的任务不抢占 |
| set_paused | paused: boolean | 持久暂停/继续派发，已运行任务继续 |
| cancel | job_id | 等待任务立即取消，运行任务保存 attempt 专属取消意图并等待执行停止 |
| crash_worker | job_id | 仅后台 --demo-mode 允许，对活动尝试注入 worker 崩溃 |
| shutdown | 无 | 停止新派发，活动任务结束后退出，等待任务继续保存在文件 |

队列上限 10000 项。`Scheduler::submitBatch()` 在同一临界区校验并写入，失败不部分入队。
`waitingOrder()` 与 `claim()` 共用比较规则，GUI 不复制策略公式。快照中的 waiting_order 仅表示当前候选顺序，不承诺设备归属。
快照每个 job 增加 slot、printed_sheets、recovering 展示字段，不改变核心 Job 快照字段。后台重启后的恢复中任务不虚构进度。

state.json 新增可选 `paused` 布尔字段，旧文件缺省 false，仍使用 schema_version 1。
快照/报表统计窗口为全部持久任务最早提交至后台 now_ms；重开窗口不重置等待和周转。
吞吐量与利用率分母包含窗口持续扩大的空闲时间。报表 mode 为 persistent_simulation，jobs 包含完整 runs 时间线以供复核。

诊断和测试可调用 `print-coordinator --state-dir <目录> --request '{"type":"snapshot"}'`，向已有服务发送一条命令并打印回复。
GUI 自动创建的是独立本地协调进程，不是 Windows Service；POSIX 传统 daemon 演示仍使用 --daemon。


## 经营扩展 v0.2（2026-09-23）

当前工作重点为经营调度。既有任务状态、取消与恢复协议保留。完整规则与假设见 [shop.md](shop.md)。

### 核心模型

`Job` 新增 `paper`（A4/A3）、`color`、`duplex`、`service`（Express/Standard/Flexible）、`deadlineMs`、`baseCents`、`quotedCents`、`costCents`、`refundCents`、`assignedPrinter`。金额为整数分；`sheets()` 保留原打印面语义，`paperSheets()` 为实际张数，双面按每一份分别向上取整。旧任务缺省 A4 黑白单面、无期限、金额 0、未分配设备。

`Printer` 包含名称、A3/彩色/双面能力、接单开关、黑白/彩色每面毫秒数。`Inventory` 是累计供应张数（A4/A3），不是可用库存；可用数从供应与完整订单快照推导。

`Scheduler::claimFor(policy, now, fleet, idle, supplied)` 在同一 mutex 内检查设备占用、兼容性、库存并领取订单。调用方先持久化再启动进程；assignedPrinter 和成本随领取持久保存。`refund(id, cumulativeCents)` 只允许完成订单，累计退款不递减且不超过报价，重复同额请求幂等。

新增 `Policy::Deadline` / `Policy::Revenue`，IPC 名称为 `deadline` / `revenue`。原回放工具保留三种历史同构策略；经营回放使用 `replayShop` / `shop-experiments`。

### 持久格式

经营目录顶层 `schema_version: 2`，旧同构目录仍为 1；读取器支持二者。v2 必须包含 `shop`：

```json
{"version":1,"fleet":[{"name":"黑白快印 01","a3":false,"color":false,"duplex":true,"online":true,"mono_page_ms":150,"color_page_ms":150}],"supplied_a4":10000,"supplied_a3":3000}
```

示例省略其余设备。fleet 长度必须等于顶层 printers（1–64）；每面延时 10–5000 ms，A3 执行时加倍。读取时校验库存未透支、设备索引与活动订单独占，校验失败不覆盖文件。每条订单保存完整经营字段，报价不会在读取时重算。新订单总页数仍不超过 10000。

### IPC 增量

IPC 信封继续 schema_version 1，与持久文件版本分别管理。

| 请求 | 输入 | 行为 |
| --- | --- | --- |
| quote | job（与 submit 同规格） | 返回基础价、报价、承诺期限、物理用纸和当前库存是否足够；不入队、不预留库存 |
| submit / submit_batch | 原字段，加 paper=`a4/a3`、color/duplex 布尔、service=`express/standard/flexible` | 后台计算价格与相对期限，客户端金额字段无权改价；整批校验后持久入队 |
| set_printer | index，online 布尔 | 改变接新单开关；当前执行不中断 |
| set_printer | index，完整 printer 对象 | 仅设备空闲时修改名称、能力与速度 |
| restock | paper，quantity（1–1000000） | 增加累计供应，单纸型累计上限 1000000000 |
| refund | job_id，cumulative_cents | 设置完成订单的累计退款额；界面提供全额退款 |
| set_policy | policy 增加 deadline/revenue | 改变后续匹配，不抢占当前订单 |

每次经营变更先持久化再回复 accepted。客户端丢失确认后不自动重发。quote 的库存只是预览，提交与预览之间可变化；实际库存以领取时的原子检查为准。系统不保证所有接受订单能按时交付，逾期单独统计。

snapshot 新增 shop_enabled、fleet、stock_a4/stock_a3、supplied_a4/supplied_a3、shop_metrics。订单新增 JSON 字段 paper/color/duplex/service/deadline_ms/base_cents/quoted_cents/cost_cents/refund_cents/assigned_printer。`slot` 仅活动时展示，assigned_printer 保留上次派发设备供审计。

shop_metrics 包含 gross_cents/refund_cents/revenue_cents/cost_cents/contribution_cents/pending_cents、completed/on_time/late/overdue。金额是完成订单口径，退款按观察时点累计归属原订单，不是会计期间现金流水。

worker Job JSON 仍为 1，新增可选 paper/color/duplex 字段；输出增加 paper/color/duplex/physical_sheets，原有 sheet 与进度字段继续表示打印面，保持原解析器兼容。
