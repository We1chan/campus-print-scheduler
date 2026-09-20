# 单任务监督进程控制协议 v1

这是为 Issue #3 取消、控制重连与演示故障补充的最小执行器接口，供 B 的持久服务后续整合。
现有不带控制选项的 supervisor 命令与 stdout JSONL 继续可用；未引入持久队列或常驻全局服务。

## 启动与连接

GUI 为每次调度领取生成新的随机令牌，启动：

```text
print-supervisor --job <file> --output-dir <directory>
  --control-token <随机32至80位令牌> --claim-attempt <Scheduler领取次数>
  [--lease-ms 8000] [--demo-mode]
```

令牌仅使用 ASCII 字母、数字、下划线、连字符；GUI 使用随机 UUID。不要把令牌写入日志或长期配置。
supervisor 仅监听 IPv4 回环 `127.0.0.1` 的系统分配端口，stdout 发出
`control_ready {port, claim_attempt}`，不在该事件中包含令牌。
无需防火墙规则、固定端口、服务安装或 socket 文件，进程退出后监听结束。

连接后发送一行 UTF-8 JSON（字段名为固定英文）：

```json
{"schema_version":1,"job_id":"demo-001","claim_attempt":1,"token":"<当前令牌>","request_id":"<本次请求ID>","command":"subscribe"}
```

每个请求校验版本、job_id、claim_attempt、令牌和 1–80 字符 request_id。单帧上限 16 KiB，支持拆包/粘包，
超限断开；最多 4 个连接，2 秒内未认证的连接关闭；慢读者的待发送数据超过 64 KiB 时断开。
端口是临时的，不可当作其他进程或重启服务的身份；每次新的 supervisor 都必须使用新的令牌。

## 命令与响应

所有响应含 schema_version、job_id、claim_attempt。

| 命令 | 响应与行为 |
| --- | --- |
| subscribe | `control_state`：state、printed_sheets、total_sheets、worker_attempt、demo_mode |
| ping | `control_ack`，更新控制租约 |
| cancel | `control_ack` 仅确认请求接收；停止退避定时器，终止当前 worker，等待 OS 确认停止 |
| crash_worker | 只在 supervisor 启用 `--demo-mode` 且 worker 正在运行时允许；结束真实 worker，沿用有限重试机制 |

`control_ack` 字段：request_id、command、accepted（布尔）、reason。拒绝命令不改变业务状态。
claim_attempt 是 A 的领取令牌；worker_attempt/worker_started.attempt 是该 supervisor 内部的执行次数，不能混用。

GUI 每秒发送 ping；有效控制请求更新租约。GUI 自动重连最多 8 次，延迟从 200 ms 增加、上限 2 秒，
重新 subscribe 后恢复控制并重发未确认的取消意图。重复 cancel 幂等。
断开演示在客户端关闭真实 TCP 连接，不杀 supervisor；stdout 进度通道仍持续工作。

租约默认 8 秒（允许 500–60000 ms）。没有心跳时 supervisor 停止 worker 后退出，避免客户端丢失后无限执行。
POSIX 忽略 stdout 的 SIGPIPE，使 GUI 管道消失时仍能走到租约清理。此保证要求 supervisor 本身仍存活，
不能替代 B 的持久协调服务、监督进程被强杀后的进程树回收与恢复对账。

## 取消与最终输出竞争

stdout 新增 `cancelling {claim_attempt}` 和 `cancelled {claim_attempt, reason}`。
只有 worker 已处于 NotRunning，且本次输出没有提交时，才发 cancelled 并正常退出 **130**。
GUI 同时要求合法 cancelled 事件和正常退出 130，才调用 A 的 acknowledgeCancel。
不把控制请求的 accepted 或 socket 断开当成取消完成。

每次 worker 执行获得独立 execution-token，作为最终文本文件首行 `execution_token=<token>`，
随内容由 QSaveFile 一次原子提交。worker 停止后 supervisor 对账该令牌：如果属于本次执行的输出已经提交，
即使取消恰好到达，也发 completed / supervisor_completed，正常退出 0。旧输出的令牌不会匹配新尝试，
取消不会把以前同 job_id 的输出误算成本次成功。execution-token 与控制认证令牌不同。

GUI 在 Cancelling 状态仍允许 A 的 finish(success=true)，让已提交结果生效。
若监督进程在取消确认前被外部强杀，保留未知的取消中状态并显示错误，不伪造停止确认。

## 可重复测试

CTest `control_integration` 使用真实进程和 TCP 连接，覆盖运行中取消、旧输出保护、重连、真实 worker 崩溃、
演示模式权限、旧领取令牌/错误认证拒绝、心跳超时、退避期间取消、控制帧拆包粘包、提交与取消竞争。
竞态用 `--demo-mode --demo-completion-delay-ms 1500` 在 worker 已原子提交后延迟退出，保证能重复触发；
普通 supervisor 模式拒绝非零的演示延迟。GUI 不使用该延迟选项。
