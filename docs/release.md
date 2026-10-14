# 老板端与学生网页发布

发布包包含老板 GUI、coordinator、supervisor、worker、回放工具、匹配 Qt 运行库，以及学生网页和 HTTP 网关。Windows 与 macOS 支持打包，三平台源码构建由 CI 验证；产物放在被忽略的 `dist/`。

## 生成与验收

先执行 Release 构建和 CTest。网页 PDF 预览需要 `python tools/setup_pdfium.py` 安装 Python 模块及许可证；Qt 构建已不包含学生桌面预览。

```sh
cmake --preset ci
cmake --build --preset ci --parallel 2
ctest --preset ci
python tools/setup_pdfium.py
python tools/package_app.py --bin-dir build/ci/bin --qt-bin "<Qt目录>/bin" --output-dir dist/campus-print
python tests/package_smoke.py --package-dir dist/campus-print
```

`--output-dir` 必须不存在。正式发布从已提交且 clean 的工作树生成，`release-manifest.json` 记录源提交与全部文件哈希；使用构建时同一套 Qt 部署工具。Windows 使用 `windeployqt`；macOS 使用 `macdeployqt` 并做 ad-hoc 签名验证。

本机 MSYS2 UCRT64 的 `windeployqt` 未自动携带全部 MinGW 和第三方 DLL；本机独立验证包需补齐同一工具链的 DLL 及许可证，再重新记录文件哈希。官方 Qt SDK/CI 的部署仍由对应工具和独立 runner 验证。

包内 `tools/classroom_web.py`、`tools/classroom/`、网页品牌素材及 `.tools/pdfium/` 支撑网页。Windows 的两个 GUI 可执行文件均为老板端，使用店主图标；不再生成 `start-student.vbs`。源码中的旧学生窗口与本地 C++ 预览已删除。

Office 完整包显式传入 `--office-dir` 和 `--preview-fixtures`，保留转换器上游签名与许可证。macOS 转换器位于主 app 之外；轻量包仍需系统 Office 引擎。步骤及六格式验收见 [office-preview.md](office-preview.md)。

CI 独立 runner 下载包、核对提交与文件哈希，在清除 Qt SDK 路径后检查老板端重开、网页 HTTP 上传/报价/下单、持久队列与实际模拟输出；Office 完整包再检查六格式网页预览。其他平台和陌生实体机器的结果按实际执行记录。

## 启动与数据

Windows 包内 `start-merchant.vbs` 打开老板端，`preview-merchant.vbs` 打开独立样例。学生使用浏览器；运行下列脚本同时打开老板窗口、网页网关和二维码页：

```powershell
python -m pip install --target runtime/classroom-python -r tools/classroom/requirements.txt
./start-classroom-demo.ps1
```

网页需要 Python >= 3.10。课堂脚本默认数据为 `runtime/classroom-demo`；若老板端已有目录，用 `-StateDirectory <绝对目录>` 指向同一后台。Qt 老板端传 `--data-dir`，网关传 `--state-dir`。配置收款图片与模拟边界见 [student-handoff.md](student-handoff.md)。

macOS 手动启动：

```sh
./CampusPrint.app/Contents/MacOS/campus-print --data-dir ./runtime/shop
python3 tools/classroom_web.py --coordinator ./CampusPrint.app/Contents/MacOS/print-coordinator --state-dir ./runtime/shop --open-browser
```

旧老板控制台仍可使用 `--classic-owner`；故障注入使用 `--demo-mode`。学生桌面角色 `--role student` 返回 64。网页局域网/HTTPS 部署见 [classroom-demo.md](classroom-demo.md)。

关闭界面或网页网关后，持久后台可继续执行。更换发布目录前按 [clean-up.md](clean-up.md) 停止对应后台并确认进程退出；运行数据与历史包保留到用户明确清理。
