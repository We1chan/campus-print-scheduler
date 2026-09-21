# Windows / macOS / Linux 开发

## 统一基线

| 项目 | 约定 |
| --- | --- |
| 语言 | C++17，不依赖编译器扩展 |
| Qt | 推荐 6.10.2；CMake 最低兼容 6.5，CI 固定 6.10.2 |
| 构建 | CMake >= 3.24、Ninja；CI 固定 CMake 3.31.6 |
| 编译器 | Windows MSVC 2022 x64；macOS AppleClang；Linux GCC |
| 测试脚本 | Python >= 3.10，纯标准库 |
| 路径/编码 | 源码 UTF-8，路径使用 Qt Unicode API |

仓库不包含 Qt SDK 或编译器，不修改机器全局 Git 换行设置。Windows ARM 和 MinGW 未加入当前验证矩阵；不要把 MinGW Qt 与 MSVC 编译器混用。

## Windows

1. 安装 Git for Windows、Visual Studio 2022 / Build Tools 的“使用 C++ 的桌面开发”，包含 Windows SDK。
2. 安装 Qt 6.10.2 **MSVC 2022 64-bit** 组件，以及 CMake / Ninja。也可使用 Qt 安装器附带的工具，并在当前终端配置 PATH。
3. 安装 Python 3.10 或以上；用 `python --version` 检查。
4. 打开 **Developer PowerShell for VS 2022**，选择 x64 开发环境。

```powershell
git clone git@github.com:We1chan/campus-print-scheduler.git
cd campus-print-scheduler
cmake --preset dev -DCMAKE_PREFIX_PATH="C:/Qt/6.10.2/msvc2022_64"
cmake --build --preset dev --parallel 2
$env:PATH = "C:\Qt\6.10.2\msvc2022_64\bin;" + $env:PATH
ctest --preset dev
.\build\dev\bin\campus-print.exe
```

上面的 Qt 路径是示例，替换成自己的安装路径。`cl` 找不到说明终端未加载 MSVC 环境；运行提示缺 Qt DLL，检查当前 PATH，或用 Qt Creator 的对应 Kit 运行。运行时必须保留 Qt 的 platform plugins。

## macOS

安装 Xcode Command Line Tools、Qt 6.10.2 macOS 桌面包、CMake 和 Ninja。Qt 官方包位于类似 `$HOME/Qt/6.10.2/macos`；Apple Silicon 使用原生编译器，避免混合 Intel Homebrew 与 ARM 库。

```sh
git clone git@github.com:We1chan/campus-print-scheduler.git
cd campus-print-scheduler
cmake --preset dev -DCMAKE_PREFIX_PATH="$HOME/Qt/6.10.2/macos"
cmake --build --preset dev --parallel 2
ctest --preset dev
./build/dev/bin/campus-print
```

若使用 Homebrew 的 Qt：`cmake --preset dev -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"`。Homebrew 版本可能与 CI 不同，提交前仍以固定 Qt 版本的 CI 为准。

## Linux / WSL2

Windows 不需要 WSL 即可正常开发。需要在 Windows 机器演示 POSIX daemon 时，另在 WSL2 Ubuntu 中 clone 和构建；不要复用 Windows 的 build 目录。

Ubuntu 24.04 安装 `build-essential cmake ninja-build python3` 和 Qt 6.10.2 的 Linux 桌面包。系统仓库的 Qt 版本可能过旧，优先使用与 CI 相同的官方 Qt 包，然后指定其路径：

若用 aqtinstall 精简下载，Linux 必须同时下载 `qtbase` 和 `icu`：`python -m aqt install-qt linux desktop 6.10.2 linux_gcc_64 --archives qtbase icu --outputdir "$HOME/Qt"`。Qt 官方预编译包需要它自带的 ICU 版本，不能只依赖 Ubuntu 系统 ICU。

```sh
cmake --preset dev -DCMAKE_PREFIX_PATH="$HOME/Qt/6.10.2/gcc_64"
cmake --build --preset dev --parallel 2
QT_QPA_PLATFORM=offscreen ctest --preset dev
```

图形界面需要桌面环境或 WSLg；命令行 worker / supervisor 不需要显示服务器。

## B 组持久 coordinator

前台启动示例：

```powershell
.\build\dev\bin\print-coordinator.exe --state-dir "$PWD\runtime\service" --socket-name campus-print-dev
```

macOS / Linux 去掉 `.exe`。状态目录包含 `state.json`、`jobs/`、`output/`、`control/` 与 `.coordinator.lock`。服务通过 QLocalServer 提供 JSONL 本地 IPC；协议见 `docs/interfaces.md`。当前 GUI 仍使用旧链路，因此该服务主要用于 B 组集成测试和 C 组后续接入。

Coordinator 每次领取任务先原子保存 Running/attempt，再启动 supervisor。服务异常重启时，不会直接把旧 Running 任务重新打印：先看最终输出，再检查 supervisor/output 两把锁，确认执行停止后才重试。Running 取消用 `<job_id>.<attempt>.cancel` marker 保留意图。

## 按任务演示监督进程

所有平台支持前台监督。下面用 PowerShell 示例：

```powershell
.\build\dev\bin\print-supervisor.exe --job examples/demo-job.json --output-dir runtime/demo --page-ms 1000
```

第一条 `worker_started` JSON 事件给出实际 worker PID。在第二个终端对**该 PID** 执行 `Stop-Process -Id <worker_pid> -Force`，观察 `worker_restarting` 和新的 PID。
macOS / Linux 使用 `kill -TERM <worker_pid>`。不要结束同名的其他项目进程。

输出经过 QSaveFile 原子提交，中途终止不会暴露半份最终 `.txt`。重试从头模拟该任务，最多重启 3 次。真实打印机已经打印出的纸张无法回滚，因此不能把该模拟保证称为真实设备 exactly-once。

## POSIX daemon 模式

该模式在构造 QCoreApplication 之前执行 double-fork、setsid、umask、chdir 和文件描述符重定向，然后 exec 自身，避免 macOS 系统框架继承 fork 后的无效状态。只支持 macOS / Linux，Windows 返回明确的“不支持”错误。

```sh
./build/dev/bin/print-supervisor --daemon --job "$PWD/examples/demo-job.json" --output-dir "$PWD/runtime/daemon"
```

`--job` 与 `--output-dir` 在 daemon 模式必须是绝对路径。父进程退出只表示已发起脱离，**任务是否成功以 `.events.jsonl` 的 `supervisor_completed` 和最终输出为准**。
`print-supervisor --daemon` 仍是有限任务监督示例。持久 coordinator 也支持 POSIX daemon，但 daemon 模式必须显式传绝对 `--state-dir`：

```sh
./build/dev/bin/print-coordinator --daemon --state-dir "$PWD/runtime/service" --socket-name campus-print-service
```

Windows 上 coordinator 和 supervisor 的 `--daemon` 都返回明确的不支持错误，使用前台进程模式。

## 构建目录与运行数据

全部可执行文件放在 `build/<preset>/bin/`，Windows 自动带 `.exe`。当前 macOS 开发版本不是 `.app` bundle，避免子进程定位受 bundle 路径影响；最终安装包由成员 C 统一设计布局与部署。

GUI 数据使用 `QStandardPaths::AppLocalDataLocation`。具体绝对路径显示在界面底部。任务 ID 是文件名，文档标题不参与路径拼接。

新增 `--data-dir runtime/gui` 可把运行数据集中在项目目录内，便于课设结束清理；
`--demo-mode` 启用真实工作进程故障与控制断线演示。批量输入和进度说明见 [gui.md](gui.md)，
Qt 部署脚本与发布验收见 [release.md](release.md)。新 GUI 自动化用例随 CTest 一起运行。

`CMakeUserPresets.json` 已被忽略，可放个人 Qt 路径；不要把别人的路径写进 `CMakeLists.txt`。换编译器或跨机器时重新生成 build 目录，不能复制 CMakeCache。

## 当前测试

- 核心：FCFS/SJF、公平并列顺序、非法参数、状态转换、4 个生产线程 + 4 个消费线程。
- 集成：实际启动 Qt GUI、worker/supervisor，中文空格路径、文件锁、坏输入、强制终止工作进程及重启、supervisor 取消 marker、POSIX daemon。
- B coordinator：持久快照 round-trip/损坏校验、单实例与 socket 名冲突、正常完成、重启后已完成任务不重复执行、Scheduler 尝试次数耗尽、不可用输出目录、Running 取消、coordinator 被杀后的恢复、平台 daemon 语义。
- Release 构建使用显式断言函数，不依赖被 NDEBUG 关闭的 `assert()`。
- CI offscreen 测试可证明启动链路，不能替代每个平台的一次人工界面验收。

参考：[Qt QProcess](https://doc.qt.io/qt-6/qprocess.html)、[QStandardPaths](https://doc.qt.io/qt-6/qstandardpaths.html)、[QLockFile](https://doc.qt.io/qt-6/qlockfile.html)、[QSaveFile](https://doc.qt.io/qt-6/qsavefile.html)。

## macOS SDK 26/27 的 Qt 版本选择

本机曾复现 Qt 6.8.3 引入旧 AGL framework 导致链接失败，统一基线因此选为 6.10.2。Qt 官方 6.10 分支已移除该 CMake 链接依赖；不修改系统 SDK，也不打补丁绕过系统检查。[上游实现](https://github.com/qt/qtbase/blob/6.10/cmake/FindWrapOpenGL.cmake)。
