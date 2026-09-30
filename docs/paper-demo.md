# 纸张工作台 Demo

该界面基于浅色概念稿实现，使用 Qt Widgets 与 QPainter，数据来自独立 coordinator 的本地 IPC 快照。

## 本机体验

在工作树根目录双击 `start-paper-demo.cmd`。脚本依次查找便携包 `dist/paper-demo`、本机历史演示包
`dist/paper-demo-20260923` 和源码构建 `build/dev/bin`；使用源码构建时会通过 `qtpaths6` 查找 Qt 运行库。
运行数据写入 `runtime/paper-demo`。脚本同时打开老板后台和学生端两个窗口；老板后台启动后自动提交一批演示订单，顶部“演示任务”可以再次加载。

发布目录不提交 Git，因此新克隆需要先完成源码构建。Windows 示例：

```powershell
cmake --preset dev -DCMAKE_PREFIX_PATH="<Qt目录>"
cmake --build --preset dev --parallel 2
```

需要给没有 Qt 开发环境的机器演示时，按 [发布说明](release.md) 生成 `dist/paper-demo` 后再复制整个目录。

通用源码运行参数：

```sh
campus-print --data-dir runtime/paper-demo --demo-mode --demo-workload --page-ms 650
campus-print --role student --data-dir runtime/paper-demo --demo-mode --page-ms 650
```

仅演示模式允许使用 --demo-workload；普通启动不会自动创建任何任务。

## 操作

- 新建任务：名称、页数、份数、优先级，或导入 JSON 批量任务。
- 工作台：点击等待文档预览；切换 FCFS / SJF / 优先级老化；暂停只停止领取新任务。
- 记录：完整任务列表、状态筛选、取消、导出报表；演示模式提供进程崩溃和连接中断。
- 设备：两台设备实时状态、统计与输出目录。
- Ctrl+K：搜索命令；F11：全屏；减少动效：立即停止纸张与队列过渡。

## 边界

纸张绘制的是任务单示意，不读取真实 PDF。输出为模拟文本文件，不连接物理打印机。
时间区域显示实时页数和共享等待顺序，不保证未来的设备归属；预计时间是按当前队列的推算，不是承诺。
当前使用持久 coordinator。关闭窗口后任务继续执行，重开相同数据目录恢复任务和统计。
启动参数中的 page-ms 与 demo-mode 只在新建后台时生效，已有后台继续使用原参数；需要更换时先正常停止后台。

## 验证

自动检查包括真实任务提交与输出、取消、控制重连、故障恢复、5000 项导入、
工作台三策略顺序、非抢占行为与小窗口控件可达性。运行截图来自 Windows 原生 Qt 窗口。
打包使用匹配的 windeployqt，并在清除 SDK 路径后验证 GUI 启动及 supervisor/worker 输出。
界面启动时会从系统实际安装的字体中选择支持中文的字族；若字体回退失效，原生截图会出现方框，
因此字体检查必须与业务测试一起保留。
这些本机检查不代表 macOS/Linux 或未安装开发环境的新机器已完成人工验收。
