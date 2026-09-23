# GUI 与持久后台集成验收

日期：2026-09-23。在主分支 `cea14c2` 的纸张工作台上完成接入，保留原有界面。
GUI 现在只持有后台快照，通过本地 IPC 提交、取消、查询和修改策略；coordinator 独立持有队列与执行进程。
提交前同步至组员新增统计查询的 `200e635`，保留 stats 命令，并让 GUI 快照与该命令共用统计序列化。

## A 的验收内容

| 场景 | 实际检查 |
| --- | --- |
| 带任务关窗、再次打开 | 提交后立即关闭；同一数据目录重开仍显示原 job_id，任务完成并输出 |
| 三种策略与暂停 | 同一批任务暂停入队，分别核对 FCFS、SJF、优先级老化的候选顺序；未领取任何暂停任务；重开仍保留策略与暂停状态 |
| 等待预览与领取一致 | 核心共用排序规则；测试使用自定义老化周期、并列项和未来到达，比较预览与实际 claim 顺序 |
| 批量提交原子性 | 直接向后台发送含重复 ID 的整批命令；拒绝整批，已有队列不增加 |
| 统计独立核算 | 导出实际执行记录，以 submitted/first_started/finished/runs 重算首次等待、累计等待、周转、最大等待、忙碌时长、吞吐量和利用率，与后台结果比较 |
| 重开后的统计 | 已完成数量、统计起点与平均首次等待保留；吞吐量和利用率随时间分母变化属于预期 |
| 取消确认后后台崩溃 | 持久取消确认后立即强杀 coordinator；GUI 自动恢复连接，原任务 Cancelled 且 attempts=1，无最终输出；另一任务正常完成 |
| 故障与重连 | 真实 worker 被演示命令终止后有限重试；GUI 断开本地连接后重连，仍能取消活动任务 |
| 状态文件兼容 | 新 paused 字段保存并恢复；旧 schema-1 文件缺少该字段时默认继续；非布尔字段拒绝加载 |

新增 GUI 场景调用真实 coordinator/supervisor/worker，使用各自独立的中文/空格临时数据目录。
测试结束会请求后台退出并等待状态目录锁释放，避免删除正在写入的数据。
逐任务核对输出页数；统计场景还核对 attempts=1 及 worker_started 事件数，防止只比较总量漏掉重复执行。

## 本机实际结果

环境：macOS 27 arm64、Qt 6.10.2、AppleClang，编译警告视为错误。

| 执行 | 结果 |
| --- | --- |
| Debug 全量 `ctest --preset dev` | 9/9 通过，33.06 秒 |
| Release 全量 `ctest --preset ci` | 9/9 通过，32.78 秒 |
| 同步组员统计接口并解决冲突后的 Release 全量回归 | 9/9 通过，32.45 秒 |
| 补充旧快照兼容与退出清理后的 GUI/runtime_state 复测 | 2/2 通过，15.56 秒 |
| 原生 Cocoa 的持久策略/报表、取消崩溃恢复、故障重连、纸张工作台 | 4 个功能场景通过；QtTest 含初始化/清理报告 6 passed |
| 修正测试清理后的原生持久报表/崩溃恢复复测 | 2 个功能场景通过 |
| 已部署 macOS 包，移除 Qt SDK 环境路径 | 原生 GUI 重开、持久暂停队列、coordinator 执行、中文路径和输出页数通过 |
| 本机重新解压归档并执行 verify_package.py | 47 个文件哈希一致；持久包测试和 8 个单任务控制场景通过；本机仍安装有 Qt SDK，不视为干净系统 |

部署测试发现：动态库路径重定位后，Apple Silicon 会因旧代码签名失效而终止 GUI（CODESIGNING / Invalid Page）。
打包脚本现用 macdeployqt 的 ad-hoc 签名选项重新签名并执行 codesign 校验。修复后的包实际运行通过；这不代表已完成 Developer ID 签名或公证。

Windows/macOS CI 的原生 GUI 步骤已加入持久报表与取消崩溃恢复场景；发布包纳入 print-coordinator。
后续两个独立 runner 仅下载发布包，不安装 Qt、不构建源代码，校验文件哈希后执行持久队列与单任务控制测试。
本记录不把本机结果替代为 Windows 结果；跨平台结果以对应提交的 [Actions](https://github.com/We1chan/campus-print-scheduler/actions/workflows/ci.yml) 为准。

本机日志与真实窗口截图位于被 Git 忽略的 `runtime/integration-a/`，构建测试日志位于 `build/*/Testing/Temporary/`。

## 复现

按 [开发环境](development.md) 配置 Qt/CMake 后，先运行完整 CTest，再按 [发布说明](release.md) 打包并测试。

```sh
cmake --build --preset ci --parallel 2
ctest --preset ci
```

仅复测原生界面时使用 `build/ci/bin/gui_tests`（Windows 为 `.exe`），传入：
`persistentPoliciesAndReport backendCrashDuringCancellation runningCancelFaultAndReconnect`。
Windows 使用 `QT_QPA_PLATFORM=windows`，macOS 使用 `cocoa`；CI 中完整环境变量写法见工作流文件。

## 保留的边界

- 当前纸张工作台为两台设备，连接到其他设备数的后台时不进入可操作状态。
- GUI 退出后后台继续运行；没有安装 Windows Service 或开机自启。停止方法见 [清理说明](clean-up.md)。
- 只保证模拟文件的状态和输出核对；真实物理打印的不可撤销副作用不在本次保证范围。
- 课堂使用的实际 Windows/macOS 机器仍需人工演示验收，记录系统版本、包版本、输入和截图。
