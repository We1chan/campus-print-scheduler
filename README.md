# 打印店经营管理与多设备协同调度系统

[![Cross-platform CI](https://github.com/We1chan/campus-print-scheduler/actions/workflows/ci.yml/badge.svg)](https://github.com/We1chan/campus-print-scheduler/actions/workflows/ci.yml)

操作系统课程设计，三人协作。使用 **C++17 + Qt 6 Widgets + CMake**，Windows 和 macOS 原生开发，Linux 用于 POSIX 守护进程验收。

## 本分支：纸间店主端与调度工作台

分支：`feat/merchant-editorial`。Windows 本地入口为根目录 `start-merchant.vbs`；完整发布包中使用 `start-merchant.vbs` 连接持久后台，或使用 `preview-merchant.vbs` 打开独立样例。

| 页面 | 主要功能 |
| --- | --- |
| 订单工作台 | 按状态筛选、搜索与分页，查看订单详情和后台打印进度，取消待打印订单 |
| 打印队列 | 按设备浏览队列、查看预计时间，翻页查看六台设备；这里也是调度工作台的入口 |
| 设备管理 | 配置现有设备槽位、接单开关、共享纸张库存；首尾页禁用对应翻页按钮 |
| 营业统计 | 今日、本周、本月或自定义日期汇总，导出后台历史报表 |

**调度工作台的打开步骤：启动店主端 → 顶栏“打印队列”（也可按 Ctrl+2）→ 点击队列标题同一行的“调度工作台”。** 此按钮位于设备分页按钮的左侧，真实模式与样例模式均可进入。

工作台在同一时间轴对比先来先服务、短作业优先、优先级老化、期限优先和收入优先；提供长短单、取件期限、等待老化、营业收入四个场景。点击订单可以在五条时间轴中同步高亮，点击策略可以查看顺序解释与指标；支持单步、播放/暂停、重置及 1×/2×/4× 速度。左上角返回按钮回到打印队列。

**当前调度工作台使用内存中的教学订单，不读取真实后台队列，也不会切换真实派发策略。** 五种结果复用调度核心计算；后台实时订单接入尚未实现。学生端传输订单元数据，不传 PDF/Word 原件。完整功能、实际截图与边界见 [店主端说明](docs/merchant-frontend.md)，联调方案见 [当前队列接入说明](docs/dispatch-live-integration.md)。

两端沿用统一字体、字号与平面按钮反馈；四页使用同尺寸纸面横幅和相同指标分割，窗口保留浅色轮廓与阴影，店铺及策略菜单使用淡灰圆角阴影。店主专用图标覆盖桌面、程序与任务栏。

## 当前交付：v0.2 经营调度

根据教师反馈，项目重点调整为打印店老板视角的订单、价格、交付时间、设备产能与营业额。故障恢复保留为后台基础能力，降低展示优先级。真实打印与支付使用模拟实现。

新增可运行功能：

- A4/A3、黑白/彩色、单双面、页数和份数；区分打印面与物理纸张。
- 六台能力、速度不同的设备，空闲时编辑能力，随时暂停或恢复接单。
- 快速、普通、灵活三种服务档位；等待换优惠，后台锁定报价与交付期限。
- 五种策略：FCFS、SJF、优先级老化、交付期限优先、营业收入导向。
- 库存预留和补货、完成结算、模拟退款、实收/成本/贡献利润/超时统计。
- 经营页一键运行 21 组可复现实验，区分固定订单的调度比较与潜在客户的优惠选择。
- 两个界面：老板后台（系统状态、设备、营收）与学生端（上传文件、报价、模拟支付、我的订单、排队情况），连接同一后台。
- 预计等待时间：后台按当前策略、设备与库存重放派发规则，给出每个订单的预计开始和完成时间，以及“此刻下单”的预计。

[经营规则与演示说明](docs/shop.md) · [实验结果](docs/shop-results.md) · [协议契约](docs/interfaces.md)

本分支提供「纸间」学生端：俯视纸张工作台、真实本地文档预览、无系统标题栏窗口、悬停展开、主题订单/队列与模拟支付。代理接手与部署先读 [学生端交接](docs/student-handoff.md)，视觉及性能证据见 [设计与验证](docs/editorial-frontend.md)。Windows 的学生入口为 `start-editorial.vbs` 或发布包 `start-student.vbs`；原 `start-studio-demo.cmd` 仍用于两端演示。

本分支另提供按四张确认概念图实现的「纸间」店主端，默认老板入口包含订单工作台、打印队列、设备管理和营业统计。运行 `--merchant-showcase` 查看独立交互样例；真实模式继续连接共同持久后台。启动方式、实际截图、功能与协议边界见 [店主端说明](docs/merchant-frontend.md)。

```sh
cmake --preset dev
cmake --build --preset dev --parallel 4
./build/dev/bin/campus-print --demo-mode --demo-workload --data-dir runtime/shop-demo   # 老板后台
./build/dev/bin/campus-print --role student --data-dir runtime/shop-demo                # 学生端，另一个终端
```

使用新的经营数据目录；原有目录不会被自动覆盖。新快照为 schema 3，支持读取 schema 1 / 2。预计时间是估算而非承诺，支付与学号均为模拟。以下是继续沿用的后台与课程基础：

| 功能 | 状态 |
| --- | --- |
| FCFS / SJF、线程安全队列、原子领取任务 | 已实现并测试 |
| Qt 订单提交界面、六台异构模拟打印机 | 已实现，旧版双设备模式保留 |
| 独立 supervisor / worker、JSONL 进程通信 | 已实现 |
| worker 崩溃后有限重试、静默超时检测 | 已实现并有真实进程测试 |
| JSON 任务文件、原子提交输出、同任务文件锁 | 已实现并测试 |
| POSIX 脱离终端的有限任务守护模式 | 已实现；Windows 使用前台监督模式 |
| Windows / macOS / Linux 自动构建测试 | 已配置，结果见 Actions |
| 内存任务取消、带令牌重试、快照校验加载 | A 核心 API 已实现；B 已接入持久执行与取消，C 界面已接控制协议 |
| 持久任务队列、独立 coordinator、应用重启恢复 | GUI 已接入持久后台，关窗继续执行，重开同步队列 |
| 优先级与老化、统计、数据集回放 | A 已实现；26 个新增测试场景、33 组策略实验与答辩材料已完成 |
| 纸张工作台、多打印机进度、JSONL 分帧、批量导入、统计图/报表 | C 已实现，见 [界面说明](docs/gui.md) 和 [演示入口](docs/paper-demo.md) |
| 等待/运行任务取消、暂停派发、控制重连、演示故障 | 已通过 coordinator 本地 IPC 集成 |
| Windows/macOS Qt 部署与包测试 | 已提供打包、校验和独立 runner 测试；人工现场验收另行记录 |

当前 `campus-print` 通过本地 IPC 使用独立 `print-coordinator`，首次连接可自动启动后台。关闭窗口后任务继续执行；用同一数据目录重开可恢复队列、策略、暂停状态与统计。后台异常后自动重连并进行恢复核对。跨平台发布包包含 coordinator，验收记录见 [统一链路验证](docs/validation-integration.md)。真实物理打印机的不可撤销副作用不在本模拟系统保证范围内。

## 三个人先看这里

- [三人分工与验收](docs/team.md)：A（@We1chan）负责调度与统计，B（@YI1990442038）负责进程与存储，C（@korwave）负责界面与跨平台集成。
- [Windows / macOS 开发环境](docs/development.md)：两套可复制命令和常见兼容问题。
- [架构与演进](docs/architecture.md)：当前进程关系、目标持久化结构与 OS 对应关系。
- [公共接口](docs/interfaces.md)：Job、状态机、JSONL、退出码，修改前先沟通。
- [里程碑与演示](docs/milestones.md)：从当前骨架推进到完整验收。
- [统计与负载回放](docs/replay.md)：自建 JSON / SWF 转换、三策略比较与统计口径。
- [A 实验报告](docs/experiments-a.md)、[专项验证](docs/validation-a.md)、[个人答辩提纲](docs/defense-a.md)：实际结果、图表、测试覆盖与代码讲解。
- [数据集策略](docs/datasets.md)：真实计算作业日志只能作为映射后的模拟打印负载。
- [协作规则](CONTRIBUTING.md)：主分支同步、提交推送与跨平台验收。
- [界面与验收](docs/gui.md)、[纸张工作台 Demo](docs/paper-demo.md)、[Qt 发布包](docs/release.md)、[项目结束清理](docs/clean-up.md)。

## 快速运行

安装 C++17 编译器、CMake >= 3.24、Ninja、Qt 6.10.2 的桌面组件，以及用于集成测试的 Python >= 3.10。
Windows 使用 **x64 Native Tools Command Prompt for VS 2022** 或 **Developer PowerShell for VS 2022**；Qt 必须匹配 MSVC x64。

```sh
git clone git@github.com:We1chan/campus-print-scheduler.git
cd campus-print-scheduler
# 将下行 Qt 路径替换为你自己机器上的安装目录：
python tools/setup_pdfium.py
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
