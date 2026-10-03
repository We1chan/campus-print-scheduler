# 店主端验收记录

2026-10-03，Windows 11，Qt 6.10.1，MinGW 13.1，`ci` Release，warnings-as-errors 开启。

## 已完成的源代码与原生验收

- 完整 CTest：14/14 通过，包括学生 GUI、店主 GUI、调度、持久状态、协调器、进程/控制链路与经营实验。
- 店主 QtTest：9 个场景（另含初始化和清理）验证基准样例静止、开始/暂停/完成/取件、兼容性与重排、拖动移动约束、报表内容、自定义日期、添加设备上限与自动测试页、导航/搜索/29 条历史分页/文件窗口，以及真实 IPC。
- Windows 原生额外执行界面和真实 IPC 两个场景：4 passed / 0 failed（含初始化/清理）。实际发送拖动进入与落下事件，核对模型顺序变化。
- 真实 IPC 使用独立的中文和空格临时目录；另一 PrintController 提交订单，店主观察 waiting→completed，核对六个打印面、¥8.55 真实报价、输出文件，以及设备接单设置转为协议 `online` 字段。只关闭测试拥有的数据目录后台。
- 四张实际截图来自 Windows 原生平台插件的 QWidget grab，统一为 1586×992；图片见 [页面说明](merchant-frontend.md)。没有以概念图替换真实渲染截图。
- 原学生工作树的 HEAD 保持 `42f77a1`，已有 12 个未跟踪概念图/说明文件保留。

源代码测试：

```powershell
cmake --build --preset ci --parallel 4
ctest --preset ci --output-on-failure --parallel 4
$env:QT_QPA_PLATFORM = 'windows'
build/ci/bin/merchant_tests.exe nativeNavigationSearchHistoryAndFileDialog persistentBackendReceivesOtherClientOrder -o runtime/merchant-evidence/native-tests.txt,txt
```

## 发布包验收

完整 Windows 包携带 Qt、MinGW、PDFium 与许可证、完整 LibreOffice、六种 Office 示例及两端启动脚本。下面三项是发布包完成条件，输出存放在本工作树的 `runtime/merchant-evidence/`：

```powershell
python tools/verify_package.py --archive-dir dist --output-dir runtime/merchant-package-check
python tests/package_smoke.py --package-dir dist/merchant-v1
python tests/office_package_smoke.py --package-dir dist/merchant-v1
```

`verify_package` 核对 manifest 的提交、clean 标记与全部 SHA-256；`package_smoke` 在移除 SDK PATH 的环境中检查原生启动、重开、持久队列、静默后台与 worker 输出；`office_package_smoke` 仅使用包内引擎，在移除 SDK PATH 后真实渲染 doc/docx/ppt/pptx/xls/xlsx 的两页输入。

这次验证范围是本机 Windows；没有将本机结果当作 macOS、Linux、远端 CI、干净新机器或物理打印验证。真实后台尚缺的功能已在 [页面说明](merchant-frontend.md) 中明确，不把样例操作当成已新增的协议能力。
