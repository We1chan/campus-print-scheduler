# 成员 C 验证记录

日期：2026-09-20、2026-09-23。范围为 Issue #3 的本地执行器、可选控制协议、
纸张工作台及主分支集成验证。

## 本机实际执行

- Windows 11 x64；复用已有 Qt 6.10.1 MinGW 64-bit、GCC 13.1.0、CMake 和 Python 3.12.14。
  这是补充验证环境；项目正式 CI 仍使用 Qt 6.10.2 / Windows MSVC、macOS AppleClang、Ubuntu GCC。
- Debug：编译成功，警告视为错误；3 个 CTest 套件全部通过。
- Release：编译成功，警告视为错误；scheduler、gui、process_integration 共 3 个套件全部通过。
- Windows 原生 GUI：6 个功能测试通过（QtTest 连同初始化/清理报告 8 passed）；中文截图经实际检查。
- 5000 个任务批量导入：GUI 测试计时器记录 9 次心跳，最长观测间隔 25 ms；文字输入仍可交互。
  这是当前机器的一次真实运行记录，不是所有机器的响应时间保证。
- JSONL 对中文消息每一个字节位置进行拆分，覆盖粘包、CRLF、版本/ID/进度校验、超长帧丢弃和 EOF 半条消息。
- 真实 GUI 提交、等待任务取消、两台执行器完成并输出；整批非法输入不部分导入；JSON 报表落盘；提交/关闭竞态保护。
- `windeployqt` 生成 Windows Release 发布目录；移除 SDK PATH、Qt 插件和动态库环境变量后，
  发布包的原生 GUI 启动、supervisor/worker 打印、中文空格路径和输出页数均通过。

## 2026-09-23 纸张工作台与主分支集成

- 在最新主分支上集成纸张工作台后，使用 Qt 6.10.1 MinGW、GCC 13.1.0 和 CMake 重新生成
  Release 构建；警告视为错误，9 个 CTest 套件全部通过，包含 A 的调度/回放/工作负载、
  B 的协调器恢复和 C 的 GUI/控制协议测试。
- 原生窗口保留排队、运行、完成、表单和小窗口五种截图；使用单控件原尺寸图复核中文缺字告警，
  确认真实字形正常，缩放后的整窗预览会造成笔画视觉丢失。GUI 仍显式选择已安装的 CJK 字体族。
- GUI 冒烟测试现在显式使用临时数据目录，测试不再写入用户级应用数据目录。
- `start-paper-demo.cmd` 会查找便携包、历史演示包或 `build/dev/bin`，源码启动时通过
  `qtpaths6` 配置 Qt 运行库；缺少可执行文件时输出构建命令，不依赖未提交的固定发布目录。
- 使用匹配的 Qt 运行时临时打包并执行 `package_smoke.py`；清除 SDK 路径后 GUI 启动以及
  supervisor/worker 打印通过。测试包仅用于验证，清理后不提交 Git。

本地自动测试数据、截图、日志和打包产物均留在忽略目录。既有 Qt 工具输出过账户授权诊断，
但命令退出成功并完成构建；未修改账户设置或使用跳过授权检查的环境变量。

## 远程验证

三平台结果应按 [fork 的 CI](https://github.com/korwave/campus-print-scheduler/actions/workflows/ci.yml)
中对应提交查看。工作流会保存 CTest 日志与 QtTest JUnit，Windows/macOS 另保存已部署 Qt 的归档包。
macOS 包使用 tar.gz 保留可执行权限和 framework 符号链接。

2026-09-23 将纸张工作台集成到主仓库后，主仓库
[运行 35830290565](https://github.com/We1chan/campus-print-scheduler/actions/runs/35830290565)
覆盖 Windows、macOS、Ubuntu，以及 Windows/macOS 两个独立发布包环境，共 5 个 job 全部成功。

## 本次继续实现的控制与部署验证

- 新增 control_integration 后，本机 Release 的 4 个 CTest 套件通过。
- 实际 TCP 与子进程用例覆盖运行中取消、旧输出保护、控制重连、worker 故障重启、非演示模式拒绝故障、
  旧 claim_attempt/错误令牌拒绝、控制租约超时、退避中取消、取消与原子提交竞争及控制命令拆包/粘包。
- Windows 原生 `runningCancelFaultAndReconnect` 场景通过，保存连接、重连后和取消完成三张真实窗口截图。
- GUI 新增运行中取消与演示权限测试；结束时检查没有遗留本项目 GUI、supervisor、worker。
- CI 增加 Windows/macOS 原生界面演示记录，以及全新 runner 下载发布包后进行的文件哈希校验、
  GUI 启动、打印及控制集成测试。独立 job 不安装 Qt、不构建源代码；实际状态以对应提交的 Actions 为准。

## 验收边界

- 尚未获得独立无 Qt 开发环境 Windows/macOS 机器上的人工验收记录。
- 单任务取消、控制连接重连和演示故障已真实执行；B 的持久服务生命周期与应用级恢复仍在 Issue #2 范围。
- 未实现应用级持久队列、真实打印机、Windows Service 或真实纸张 exactly-once。
- 原生 Windows 截图来自真实运行；Linux/macOS offscreen 启动和 CI 不等于两平台人工演示。
