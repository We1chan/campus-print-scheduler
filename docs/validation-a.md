# A：专项验证记录

日期：2026-09-23。测试和快照校验修复提交为 [`2e9567e`](https://github.com/We1chan/campus-print-scheduler/commit/2e9567e0193aed68c8060b9e9a4e900839400bce)，实验工具提交为 [`43209c1`](https://github.com/We1chan/campus-print-scheduler/commit/43209c1c241a888c031458cad85cef855b0e569a)。本记录描述实际执行结果，范围为 A 的核心功能及已有项目测试的回归。

## 自动化覆盖

新增 **3 个 CTest 测试套件、26 个命名场景**。每个场景包含多个断言，场景数不等于 CTest 套件数或线程执行次数。

| 文件 / 套件 | 场景数 | 验证点 |
| --- | ---: | --- |
| [scheduler_advanced_tests.cpp](../tests/scheduler_advanced_tests.cpp) / `scheduler_advanced` | 11 | 老化边界及并列规则；未来到达与非抢占；重试累积等待及过期令牌；次数上限与时钟；取消状态；完成与取消竞争；领取与取消竞争；多生产者/消费者领取身份唯一；快照隔离及恢复；畸形快照原子拒绝；参数边界 |
| [metrics_replay_tests.cpp](../tests/metrics_replay_tests.cpp) / `metrics_replay` | 8 | 手算单设备排序与指标；同刻到达/双设备/空闲；完成与到达同刻；重试与不同结局统计分组；窗口裁剪及空样本；设备容量和窗口边界；回放非法输入与溢出；确定性、唯一派发和工作量守恒 |
| [workload_tools_test.py](../tests/workload_tools_test.py) / `workload_tools` | 7 | SWF 映射/筛选/来源；offset/默认优先级/样本不足；非法参数与 HTML；CLI 手算结果与重复输出；未来到达/空负载；任务边界与旧报告保护；schema/命令参数/输入保护 |

并发场景用 C++17 条件变量统一释放线程；两类竞争各重复 40 轮，另有 4 个生产者和 4 个消费者并发。检查领取到的 job_id 集合与唯一性，避免仅比较总数而漏掉“某任务重复、另一任务遗漏”。这属于多次竞争回归，不是对全部线程交错的形式化证明。

核心测试显式传入时间或注入时钟，精确时间断言不依赖 `sleep`。新增 C++ 测试使用抛异常的校验器，Release 下仍有效，不依赖可能被 `NDEBUG` 禁用的 `assert`。命令行测试在中文及空格临时路径中调用真实转换器和回放程序，用自建 SWF 小样本，不访问网络。

手算基准：3 个同时到达的任务为 20 / 2 / 5 页，单设备 1 秒/页。FCFS 平均等待 `(0+20+22)/3=14` 秒；SJF 为 `(0+2+7)/3=3` 秒。两者总工作时长同为 27 秒，平均周转分别为 23 / 12 秒。预期值独立于实现生成。

## 本次发现并修复的缺陷

畸形快照测试发现：恢复 Running 等状态时，原校验未要求 `queuedSinceMs` 与执行历史中的实际入队时间一致；Running 的 `lastChangedMs` 也可偏离本次开始时间。状态表面合法但时间字段矛盾，会破坏恢复后的时间线语义。

修复在 [scheduler.cpp](../src/core/scheduler.cpp) 的 `validSnapshot` 中，根据每次运行区间重建最近入队时间，核对 Running/Cancelling/Completed/Failed 的队列时间，并分别验证等待中取消与执行中取消的合法历史。所有输入先完成校验，再载入空调度器；失败保持目标调度器为空。未改变公共 API 或 JSON schema。12 种畸形快照变体和正常各状态恢复一并验证。

## 实际结果

| 环境与命令 | 结果 |
| --- | --- |
| 本机 macOS 27 arm64，纯 C++ Debug，`ctest --preset core` | 3/3 通过 |
| 本机 macOS 27 arm64，Qt 6.10.2 Debug，`ctest --preset dev` | 9/9 通过，约 19.52 秒 |
| 本机 macOS 27 arm64，Qt 6.10.2 Release，`ctest --preset ci` | 9/9 通过，约 20.00 秒 |
| `43209c1` GitHub Actions：Windows / macOS / Ubuntu 构建与测试 | 3 个 job 均通过 |
| 同一 CI：Windows / macOS 独立 runner 验证已有桌面包 | 2 个 job 均通过 |

可追溯 CI：[测试提交运行](https://github.com/We1chan/campus-print-scheduler/actions/runs/35753163075)、[实验工具提交运行](https://github.com/We1chan/campus-print-scheduler/actions/runs/35753663059)。两次工作流已完成且结论为 success；测试证据随对应工作流上传，受 GitHub artifact 保留期限制。

全量 9 个套件为 `scheduler`、`scheduler_advanced`、`metrics_replay`、`gui`、`runtime_state`、`coordinator_integration`、`workload_tools`、`process_integration`、`control_integration`。其中 B/C 的已有套件仅作为回归验证，不记为 A 新增用例。

本次 33 组离散事件实验也全部通过独立记录核算与重复输出检查，详见 [实验报告](experiments-a.md)。它们在本机实际执行；CI 执行的是离线小样本工具测试，没有在三个 runner 上重新下载和运行整套 SDSC 实验。

## 后续复测

按开发文档安装依赖、完成 CMake 配置后：

```sh
cmake --build --preset dev --parallel 2
ctest --preset dev
cmake --build --preset ci --parallel 2
ctest --preset ci
```

只运行新增套件可用 `ctest --preset dev -R "scheduler_advanced|metrics_replay|workload_tools"`。本机详细日志位于 `build/dev/Testing/Temporary/LastTest.log` 和 `build/ci/Testing/Temporary/LastTest.log`，均被 Git 忽略。

待团队完成的验收：GUI 改用独立 coordinator 的持久队列；策略、取消、重连、崩溃恢复在统一链路上的联调；包含 coordinator 的最终发行包与实际演示机器验收。现有包测试成功不意味着这些尚未集成的内容已经完成。
