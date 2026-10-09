# Qt 发布包与验收

发布包包含 GUI、coordinator、supervisor、worker、回放工具，以及匹配的 Qt 运行库和 platform plugins。
支持 Windows 与 macOS；三平台源码构建仍由 CI 验证。打包产物不提交 Git。

学生端的模块、配置、代理交接和部署完成条件以 [student-handoff.md](student-handoff.md) 为入口；设计验收见 [editorial-frontend.md](editorial-frontend.md)。发布包也携带这两份文档。

## 生成与测试

先完成 Release 构建和 CTest，再用**构建时同一套 Qt** 的部署工具：

首次构建前执行 `python tools/setup_pdfium.py`，然后 configure/build；PDFium 原生库与许可证目录都须存在。打包记录当前提交和工作树状态，正式交付从已提交且 clean 的工作树生成。

```sh
python tools/package_app.py --bin-dir build/ci/bin --qt-bin "<Qt目录>/bin" --output-dir dist/campus-print
python tests/package_smoke.py --package-dir dist/campus-print
```

`--output-dir` 必须不存在，脚本不会删除或覆盖旧包。Windows 调用 `windeployqt`，macOS 调用
`macdeployqt` 并创建 `CampusPrint.app`。所有子进程在 GUI 的可执行文件目录，保持现有定位接口。
Windows 分发整个目录；macOS 的 Office 完整包也分发整个目录，包含 `CampusPrint.app` 与同级 `office/LibreOffice.app`。不能单独复制 GUI 可执行文件，或在 Office 完整包中只拿走主 app。

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

上述默认打开老板端；学生端使用 `--role student`，或 Windows 双击包内 `start-student.vbs` 静默启动。两端传入相同 `--data-dir` 才共享队列。发布包自带用户授权公开的两张收款码；该数据目录的 `payment-codes/` 可覆盖默认图片，换收款人需分别配置微信和支付宝。真实码扫码转账与应用内模拟确认的边界见交接文档。

`feat/merchant-editorial` 分支的默认老板入口为新的四页店主端；Windows 包提供 `start-merchant.vbs`（真实队列）和 `preview-merchant.vbs`（独立交互样例）。包内学生/店主启动脚本共享 `runtime/shop`。操作说明与原生截图见 [merchant-frontend.md](merchant-frontend.md)，旧控制台可用 `--classic-owner` 打开。

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
## Office 文档分发

Word/PPT/Excel 预览须携带完整 LibreOffice 转换器或明确依赖系统安装。打包与六格式独立运行验收见 [Office 预览部署](office-preview.md)；轻量 PDF-only 包不能标为完整 Office 预览版。
