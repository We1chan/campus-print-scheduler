# 项目结束后的文件清理

本项目无需新增 Windows 服务、计划任务、数据库或防火墙规则。优先复用已有开发工具。
coordinator 使用 Qt 本地 IPC，Unix 下端点和端点互斥锁可能位于系统临时目录，正常退出时由 Qt 清理。
单任务可选控制端点仅监听回环临时端口。GUI 退出不会停止持久后台。
源码在一个 clone 目录内；下列项目产物均应放入 clone 内且已被 Git 忽略：

| 路径 | 内容 | 保留条件 |
| --- | --- | --- |
| `build/` | CMake、编译文件、测试日志 | 需要继续构建时保留 |
| `dist/` | Qt 发布包 | 交付/备份前保留 |
| `runtime/` | 任务 JSON、模拟输出、测试临时目录、截图和报表 | 验收结束并备份后可删 |
| `.tools/`、`.venv/` | 如有使用，项目专用工具和环境 | 项目结束可删 |
| `CMakeUserPresets.json` | 本机编译器与 Qt 路径 | 重建环境时需要 |

开发运行统一显式传 `--data-dir runtime/gui`。测试可通过当前进程的 TEMP/TMP 或 TMPDIR
指向 `runtime/test-temp`，不会修改系统环境变量。

可以通过同一套二进制请求停止指定数据目录的后台；后台等待正在执行的任务完成后退出，暂停/等待任务留存于状态文件：

```sh
./build/dev/bin/print-coordinator --state-dir runtime/gui --request '{"type":"shutdown"}'
```

Windows PowerShell 使用 `./build/dev/bin/print-coordinator.exe`。收到 accepted 仅代表接收关闭请求；确认该目录 `.coordinator.lock` 消失后再清理。需要尽快停止时先在 GUI 取消活动任务。后台没有设置开机自启。

结束时先确认代码已推送、课设报告和截图已经备份，且 GUI、coordinator、supervisor、worker 均已退出，
然后删除**这个项目的 clone 目录**即可清掉上述文件。删除前重新核对绝对路径，不运行模糊名称或全盘匹配删除。
如果曾不带 `--data-dir` 运行 GUI，还需按界面显示的实际目录核对那一份运行数据。

既有 Qt、MinGW/MSVC、CMake、Python、Git 和 Codex 公共运行时属于共享软件，不因为项目结束一起卸载。
共享软件可能自行记录日志，不能仅凭文件时间判断为项目专属并删除。
此文档仅记录清理边界；不会在任务交付时自动删除仍需使用的源码与验收材料。
