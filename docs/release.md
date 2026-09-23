# Qt 发布包与验收

发布包包含 GUI、coordinator、supervisor、worker、回放工具，以及匹配的 Qt 运行库和 platform plugins。
支持 Windows 与 macOS；三平台源码构建仍由 CI 验证。打包产物不提交 Git。

## 生成与测试

先完成 Release 构建和 CTest，再用**构建时同一套 Qt** 的部署工具：

```sh
python tools/package_app.py --bin-dir build/ci/bin --qt-bin "<Qt目录>/bin" --output-dir dist/campus-print
python tests/package_smoke.py --package-dir dist/campus-print
```

`--output-dir` 必须不存在，脚本不会删除或覆盖旧包。Windows 调用 `windeployqt`，macOS 调用
`macdeployqt` 并创建 `CampusPrint.app`。所有子进程在 GUI 的可执行文件目录，保持现有定位接口。
Windows 分发整个目录；macOS 分发整个 `.app`。不能单独复制 GUI 可执行文件。

CI 在 Windows/macOS 生成发布目录和文件 SHA-256 清单，清除 Qt SDK 环境变量后运行 GUI 启动和实际打印测试，上传对应产物。
后续独立 job 使用新的 runner，只下载包，不安装 Qt/CMake/编译器、不构建程序；校验提交号和所有文件哈希，
再执行原生 GUI 重开、持久队列、中文打印以及取消/重连/故障控制测试。测试前确认 PATH 没有 Qt SDK 工具。
这提供独立 CI 部署验收；课堂现场人工操作和特定版本干净系统兼容性仍应单独记录。
macOS 包在部署动态库后做本地 ad-hoc 签名并校验，避免 Apple Silicon 拒绝被重定位后的代码。
这不等于 Developer ID 签名或 notarization；正式对外分发前另行处理发布签名和第三方许可材料。

## 运行与数据位置

Windows：

```powershell
.\campus-print.exe --data-dir .\runtime
```

macOS：

```sh
./CampusPrint.app/Contents/MacOS/campus-print --data-dir ./runtime
```

可添加 `--demo-mode` 显示并启用真实 worker 崩溃、控制通道断开演示按钮。默认模式隐藏这些按钮。
不指定 `--data-dir` 时沿用 Qt 的用户数据目录；具体位置显示在界面底部。
为了课程结束方便清理，建议总是指定项目目录内的 `runtime/`。

## 独立机器验收记录（实际执行后填写）

| 平台与机器 | Qt 开发环境不存在 | 可启动 GUI | 提交中文任务并完成 | 双进度与长队列 | 记录 |
| --- | --- | --- | --- | --- | --- |
| Windows | 待验证 | 待验证 | 待验证 | 待验证 | 不以 CI 代替 |
| macOS | 待验证 | 待验证 | 待验证 | 待验证 | 不以 CI 代替 |

自动化证据：三平台 CTest；Windows/macOS 原生 GUI 演示截图；两个 `Independent runtime` job 的包校验与实际执行日志。

验收时保留系统版本、包对应提交、输入与报表、真实截图。仅复制包及示例到新机器，
不要设置开发机 Qt 的 PATH。两台机器分别操作提交、暂停派发、取消等待任务、错误输入和关窗后继续执行。

## 清理

关闭 GUI 后后台仍存在。先按 [清理说明](clean-up.md) 停止对应数据目录的后台，确认退出后再删除发布目录和运行数据。
源代码、构建产物与本地数据的清理边界见 [clean-up.md](clean-up.md)。
