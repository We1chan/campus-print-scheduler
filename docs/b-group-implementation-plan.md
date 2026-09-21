# B 组实施计划：进程、IPC、守护、文件锁与持久化

## 目标

把当前“GUI 持有内存 Scheduler、直接启动 supervisor”的 v0.1 结构推进为可恢复的后台执行基础：独立 coordinator 持有 Scheduler、持久化快照和打印机槽位；GUI 后续只通过本地 IPC 提交、查询和取消。

## 设计选择

- 持久化：JSON 快照 + `QSaveFile` 原子替换，先完成单机课程设计所需一致性，不同时维护 SQLite。
- IPC：`QLocalServer` / `QLocalSocket` + versioned JSONL。
- 调度：继续调用 A 组现有 `Scheduler`，B 不复制算法。
- 执行：coordinator 管理固定数量 supervisor；supervisor 继续负责单任务 worker 的超时和有限重启。
- 恢复：每次 claim 后先持久化 Running，再启动 supervisor；重启时结合原子输出文件和 supervisor 锁对账。

## Phase 1：持久化快照

实现完整 Job 快照、策略、SchedulerOptions 的 JSON 原子读写，并验证 round-trip、非法 schema 和损坏 JSON。

状态：**已完成**。实现位于 `include/cps/runtime/state_store.hpp`、`src/runtime/state_store.cpp`，测试为 `tests/runtime_state_tests.cpp`。

## Phase 2：独立 print-coordinator

新增 `print-coordinator`，持有 Scheduler、状态目录和打印机槽位。任务领取后必须先持久化，再写 worker Job JSON 并启动 supervisor。

状态：**已完成**。实现位于 `src/coordinator/main.cpp`。

## Phase 3：恢复、取消与正常关停

启动时 restore；Queued 继续调度；旧 Running/Cancelling 先观察输出和 supervisor 锁再做 finish/retry/acknowledgeCancel。Running 取消使用 attempt 唯一 cancel marker 通知 supervisor，让 supervisor 先结束 worker。

状态：**已完成**。恢复同时检查 supervisor/output 锁；POSIX stdout 忽略 SIGPIPE，避免 coordinator 崩溃后 supervisor 因转发管道断开而退出。

## Phase 4：本地 IPC 与单实例

支持 `submit`、`snapshot`、`cancel`、`set_policy`、`shutdown`，消息均为 schema_version=1 JSONL 并携带 request_id；状态目录使用 QLockFile 保证 coordinator 单实例。

状态：**已完成**。同时补充 POSIX `--daemon`，Windows 明确拒绝传统 daemon。

## Phase 5：测试与文档

覆盖快照 round-trip、正常提交完成、恢复、取消、重试、第二实例争锁、中文路径和损坏状态；同步更新接口和架构文档。

状态：**已完成代码与测试入口**。新增 `runtime_state`、`coordinator_integration`，并扩展 `process_integration` 的取消场景；覆盖单实例与 socket 名冲突、已完成任务不重复执行、Scheduler 尝试次数耗尽、不可用输出目录、Coordinator 崩溃恢复和平台 daemon 语义。本机缺少 Qt6/CMake，完整 Qt6 构建需由三平台 CI 或配置好 Qt6 的开发机最终验证。

## 实施约束

- 不复制 A 的 Scheduler 逻辑。
- 不让 C 直接读写持久状态文件。
- Scheduler attempt 与 supervisor 内部 worker restart 分开。
- 不在 Running 状态持久化成功前启动 supervisor。
- 不把模拟文件输出描述成真实打印 exactly-once。
