# Qt 发布包与验收

发布包包含 GUI、supervisor、worker、回放工具，以及匹配的 Qt 运行库和 platform plugins。
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

CI 在 Windows/macOS 生成发布目录，清除 Qt SDK 环境变量后运行 GUI 启动和实际打印测试，再上传对应产物。
这些检查可发现依赖遗漏，但 runner 上仍安装过 Qt，不能等同于“无开发环境机器已验收”。
此课设包未签名、未做 macOS notarization；正式对外分发前另行处理签名和第三方许可材料。

## 运行与数据位置

Windows：

```powershell
.\campus-print.exe --data-dir .\runtime
```

macOS：

```sh
./CampusPrint.app/Contents/MacOS/campus-print --data-dir ./runtime
```

可添加 `--demo-mode` 显示故障控制接口的接入状态。当前 B 未提供控制接口，故障按钮禁用。
不指定 `--data-dir` 时沿用 Qt 的用户数据目录；具体位置显示在界面底部。
为了课程结束方便清理，建议总是指定项目目录内的 `runtime/`。

## 独立机器验收记录（实际执行后填写）

| 平台与机器 | Qt 开发环境不存在 | 可启动 GUI | 提交中文任务并完成 | 双进度与长队列 | 记录 |
| --- | --- | --- | --- | --- | --- |
| Windows | 待验证 | 待验证 | 待验证 | 待验证 | 不以 CI 代替 |
| macOS | 待验证 | 待验证 | 待验证 | 待验证 | 不以 CI 代替 |

验收时保留系统版本、包对应提交、输入与报表、真实截图。仅复制包及示例到新机器，
不要设置开发机 Qt 的 PATH。两台机器分别操作提交、暂停派发、取消等待任务、错误输入和关闭保护。

## 清理

先等待运行任务结束并退出应用，再删除复制出的发布目录和指定的 `runtime/`。
源代码、构建产物与本地数据的清理边界见 [clean-up.md](clean-up.md)。
