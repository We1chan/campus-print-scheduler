# 校园共享打印任务调度与故障恢复系统

[![Cross-platform CI](https://github.com/We1chan/campus-print-scheduler/actions/workflows/ci.yml/badge.svg)](https://github.com/We1chan/campus-print-scheduler/actions/workflows/ci.yml)

操作系统课程设计，三人协作。使用 **C++17 + Qt 6 Widgets + CMake**，Windows 和 macOS 原生开发，Linux 用于 POSIX 守护进程验收。

## 当前交付：可运行的协作起点

这是 v0.1 开发骨架，**尚未完成全部课设功能**。真实打印机接入不在第一版范围内，以独立工作进程向文本文件输出模拟打印。

| 功能 | 状态 |
| --- | --- |
| FCFS / SJF、线程安全队列、原子领取任务 | 已实现并测试 |
| Qt 任务提交界面、两台模拟打印机 | 已实现 |
| 独立 supervisor / worker、JSONL 进程通信 | 已实现 |
| worker 崩溃后有限重试、静默超时检测 | 已实现并有真实进程测试 |
| JSON 任务文件、原子提交输出、同任务文件锁 | 已实现并测试 |
| POSIX 脱离终端的有限任务守护模式 | 已实现；Windows 使用前台监督模式 |
| Windows / macOS / Linux 自动构建测试 | 已配置，结果见 Actions |
| 内存任务取消、带令牌重试、快照校验加载 | A 核心 API 已实现；B 已接入持久执行与取消，C 界面已接控制协议 |
| 持久任务队列、独立 coordinator、应用重启恢复 | B 后端已实现；GUI 尚未切换到 coordinator |
| 优先级与老化、统计、数据集回放 | A 核心与命令行已实现；新增测试和实验材料后续补充 |
| 双打印机进度、JSONL 分帧、异步批量导入、统计图/报表 | C 已实现，见 [界面说明](docs/gui.md) |
| 等待/运行任务取消、暂停派发、控制重连、演示故障 | 已实现；通过可选单任务监督控制协议集成 |
| Windows/macOS Qt 部署与包测试 | 已提供打包、校验和独立 runner 测试；人工现场验收另行记录 |

当前 `campus-print` GUI 仍沿用旧的“界面内 Scheduler + 直接启动 supervisor”链路，因此关闭 GUI 仍会丢失这条旧链路的内存队列。B 组新增的 `print-coordinator` 已能独立持有持久队列、恢复未完成任务并提供本地 IPC；待 C 将 GUI 切换到该接口后，关闭 GUI 才不会影响后台任务。请勿把当前版本描述成真实物理打印机的 exactly-once 系统。

## 三个人先看这里

- [三人分工与验收](docs/team.md)：A（@We1chan）负责调度与统计，B（@YI1990442038）负责进程与存储，C（@korwave）负责界面与跨平台集成。
- [Windows / macOS 开发环境](docs/development.md)：两套可复制命令和常见兼容问题。
- [架构与演进](docs/architecture.md)：当前进程关系、目标持久化结构与 OS 对应关系。
- [公共接口](docs/interfaces.md)：Job、状态机、JSONL、退出码，修改前先沟通。
- [里程碑与演示](docs/milestones.md)：从当前骨架推进到完整验收。
- [统计与负载回放](docs/replay.md)：自建 JSON / SWF 转换、三策略比较与统计口径。
- [数据集策略](docs/datasets.md)：真实计算作业日志只能作为映射后的模拟打印负载。
- [协作规则](CONTRIBUTING.md)：分支、提交、PR 与跨平台验收。
- [界面与验收](docs/gui.md)、[Qt 发布包](docs/release.md)、[项目结束清理](docs/clean-up.md)。

## 快速运行

安装 C++17 编译器、CMake >= 3.24、Ninja、Qt 6.10.2 的桌面组件，以及用于集成测试的 Python >= 3.10。
Windows 使用 **x64 Native Tools Command Prompt for VS 2022** 或 **Developer PowerShell for VS 2022**；Qt 必须匹配 MSVC x64。

```sh
git clone git@github.com:We1chan/campus-print-scheduler.git
cd campus-print-scheduler
# 将下行 Qt 路径替换为你自己机器上的安装目录：
cmake --preset dev -DCMAKE_PREFIX_PATH="<Qt安装目录>"
cmake --build --preset dev --parallel 2
ctest --preset dev
```

macOS / Linux：`./build/dev/bin/campus-print`

Windows PowerShell：`.\build\dev\bin\campus-print.exe`

只做调度核心时不需要 Qt 或 Python：

```sh
cmake --preset core
cmake --build --preset core
ctest --preset core
```

命令行模拟打印（Windows 给可执行文件加 `.exe`）：

```sh
./build/dev/bin/print-supervisor --job examples/demo-job.json --output-dir runtime/demo
```

输出 `runtime/demo/demo-001.txt` 与监督事件 `demo-001.events.jsonl`。完整的守护模式和故障演示见 [开发文档](docs/development.md)。

## 目录

```text
include/cps/core/       纯 C++ 公共调度模型与接口（A）
src/core/              调度实现（A）
include/cps/runtime/   进程与文件接口（B；接口变更需评审）
src/runtime/           JSON、路径、后续存储（B）
src/worker/            模拟打印工作进程（B）
src/supervisor/        独立监督、重启与超时（B）
src/platform/          POSIX 与 Windows 平台适配（B）
src/gui/               Qt Widgets 界面与异步事件适配（C）
tests/                 核心并发及真实进程集成测试（各自负责模块，C 集成）
examples/              无真实个人文档的演示数据
docs/                  设计、协作与验收说明
.github/               三平台 CI、Issue / PR 模板
```

## 跨平台约定

- 文件名与源码统一 UTF-8、Git 统一 LF；MSVC 启用 `/utf-8`。
- 使用 `QDir`、`QFileInfo`、`QStandardPaths`，不写死盘符、用户名或 `/tmp`。
- 子进程用 `QProcess` 的程序名与参数列表启动，不拼接 shell 命令。
- 业务核心不包含 `windows.h`、`unistd.h` 或 Qt GUI 头文件。
- `fork/setsid` 仅在 `src/platform/`，Windows 编译不会接触 POSIX 头文件。
- 构建目录、Qt 路径和 IDE 用户配置不提交；每台机器独立构建。
- CI 检查中文及空格路径、文件互斥、真实子进程死亡和重启。

仓库为私有，管理员为 @We1chan；已向 @YI1990442038 和 @korwave 发出协作者邀请，接受后即可访问。当前未选择开源许可。
