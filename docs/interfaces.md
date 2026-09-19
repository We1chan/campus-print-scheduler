# 公共接口 v1

这是三人协作的起始契约。字段含义、状态转换与退出码的变更必须同 PR 更新此文档。

## 核心 C++ 模型

定义：`include/cps/core/scheduler.hpp`。

`Job`：`id`、UTF-8 `title`、`pages`、`copies`、`submittedMs`、`state`。
`submittedMs` 用于排序；相同时间保持插入顺序。SJF 以 `pages * copies` 排序，假设所有打印机同速。

- `submit(Job) -> bool`：验证后入队，重复 ID 返回 false。
- `claim(Policy) -> optional<Job>`：锁内完成选取与 Queued → Running，不存在“选出但未占用”的窗口。
- `finish(id, success) -> bool`：只允许 Running → Completed / Failed。
- `snapshot() -> vector<Job>`：返回独立副本，调用者不持有内部锁或引用。

当前状态机：`Queued → Running → Completed | Failed`。取消、持久恢复、重排队尚未实现；扩展时由 A 与 B 一起明确状态和幂等边界。

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
stdout 为 UTF-8 JSON Lines，一行一个对象；stderr 为诊断文本。接收方应缓存未完成的一行，不能假设一次 readyRead 对应一个对象。

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
