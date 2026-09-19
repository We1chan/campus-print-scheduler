# 初始化验证记录

日期：2026-09-19。实际测试结果应与对应提交和 GitHub Actions 一起查看。

## 本机

- macOS SDK 27.0 / AppleClang 21 / arm64。
- Qt 6.10.2，C++17，Debug，编译警告视为错误。
- 已编译核心库、GUI、worker、supervisor。
- CTest 覆盖调度与多线程领取、中文空格路径、真实工作进程崩溃重启、互斥锁、原子输出、非法输入、重试上限、GUI offscreen 启动和 POSIX daemon。
- Windows 特有的 daemon 拒绝行为在 Windows CI 执行；POSIX daemon 在 Windows 跳过。

## 远程

[三平台工作流](https://github.com/We1chan/campus-print-scheduler/actions/workflows/ci.yml) 使用 Release 构建，覆盖 Windows/MSVC、macOS/AppleClang、Ubuntu/GCC。以每次提交对应的绿色检查为准，配置工作流不等于测试通过。

## 本次发现并修复

1. Qt 6.8.3 的旧 AGL 链接与 macOS SDK 27 不兼容：统一使用 Qt 6.10.2。
2. macOS 系统框架不能在 fork 子进程中继续沿用继承状态：daemon 脱离后 exec 自身，再初始化 Qt。
3. Windows CRT 文本输出会把转发的 CRLF 变成 CRCRLF，破坏 JSONL 分帧：IPC stdout 使用二进制模式，并增加原始字节检查。
4. Linux 精简安装仅选 qtbase 缺失 ICU 73：CI 同时安装官方 `icu` archive。

## 尚未验证 / 未完成

真实硬件打印、安装包部署、应用级持久队列恢复、Windows Service、网络共享磁盘、Windows ARM、macOS Intel 机器的人工界面操作，均不在当前“已通过”声明中。
