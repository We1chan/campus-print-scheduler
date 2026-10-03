# Office 本地预览：实现、部署与验收

Status: implemented; read the validation section for verified platforms.
Scope: korwave/campus-print-scheduler, feat/print-editorial, issue #1.

## 执行链路与边界

`StudentWindow::setDocument` → `OfficePreview::start` → 独立 LibreOffice headless 进程 → 临时 PDF → `PrintProof::loadDocument` → `DocumentPreview` / PDFium。支持 `.doc/.docx/.ppt/.pptx/.xls/.xlsx`，PDF、图片、UTF-8 文本沿用原路径。转换器是运行依赖，不是 Python 文档库；Python 库只生成测试输入。

Office 原件不修改。复制到系统临时目录 `paperroom-office-*` 后转换；配置、输出及副本只属于当前会话。每次会话使用独立 `UserInstallation`，不接管已打开的 Office/LibreOffice，不复用个人宏配置。宏安全级别设为 3。Windows 使用私有 Job Object 管理转换进程及子进程；Qt >=6.7 的 POSIX 版本使用新 session/process group。超时 90 秒；替换、移除或窗口销毁取消当前任务并删除临时目录。异步错误回调携带 generation，旧文件的回调不得覆盖新文件。

GUI 不等待转换进程完成。转换期间保留导航、打印规格和移除入口；显示正在准备预览，禁止下单及手动填写猜测的页数。成功后以 PDFium 页数自动填写，Office 页数控件只读。失败时显示安装缺失、启动失败、文件不符/损坏、密码保护或超时提示；不以文件名/文本抽取冒充真实预览。输入与转换 PDF 各不超过 64 MiB，PDF 1–1000 页。

Word 保留分页；PPT 保留幻灯片尺寸；Excel 使用 `calc_pdf_Export`，保留打印区域、分页及纸张缩放，不开启 `SinglePageSheets`，不将表格强行压成一页。结果依赖 LibreOffice 导入及机器字体，不能承诺与 Microsoft Office 在所有字体/复杂对象上像素一致。

文档字节及临时 PDF 路径均不进入 IPC。提交给后台的仍是原文件名、实际页数、份数及现有打印规格。此变更不提供网络文件上传、真实打印或到账确认。

## 横向页面与旋转

- 页面尺寸按每页读取并缓存，仅在文档/页码/方向变化时更新纸张几何；动画不逐帧查询 PDFium。
- 横向 PPT 自动以横向纸张展示，内容保持原比例。混合方向 PDF 按当前页切换。纸张使用统一缩放，窗口宽高变化不会拉伸内容；阅读器的“适合窗口”同时约束宽高。
- 工具栏第四个按钮 `rotateProof` 为“旋转预览”，顺时针 90°，四次恢复。桌面纸张与阅读器采用同一旋转方向，替换/移除文档复位。它不改变原文件、页数或打印规格；真实打印方向设置属于后续打印管线。

## 运行依赖与打包

发现顺序：`CPS_OFFICE_EXECUTABLE` 显式绝对路径（错误时不静默回退）→ 可执行目录内完整 `office/` → 系统常见 LibreOffice 安装路径 → PATH。Windows 包内为 `office/program/soffice.com`；macOS 为 `CampusPrint.app/Contents/MacOS/office/LibreOffice.app/Contents/MacOS/soffice`。不要只复制 soffice 单个文件。

Windows 私有部署，Python >=3.11：

```powershell
python tools/setup_office_preview.py
$env:CPS_OFFICE_EXECUTABLE = (Resolve-Path '.tools/libreoffice/program/soffice.com').Path
```

脚本使用 LibreOffice 26.8.0 的 x64 MSI，SHA-256 固定为 `4aa6c6e1895f4055104effcb556bd3362d20c6ad707c149543304f395ef9db95`，匹配上游 winget 26.8.0.3 manifest。先校验后执行 `msiexec /a /qn TARGETDIR=...`，生成私有行政镜像，不执行 `/i` 系统安装。输出目录必须不存在；保留旧镜像。可传入 `--installer <已下载MSI>`。不跳过校验，不全局禁用 Office 安全设置。

Windows 转换器还依赖 Microsoft Visual C++ x64 运行库；MSVC 的 Qt 分发会携带匹配的运行库，MinGW 的 Qt 分发不能代替此依赖。部署到新机器时检查 VC++ 运行库及实际导入结果。macOS：安装 LibreOffice（CI 使用 `brew install --cask libreoffice`）；路径为 `/Applications/LibreOffice.app/Contents/MacOS/soffice`。Linux：安装发行版的 LibreOffice（CI `apt-get install libreoffice`），使用 `/usr/bin/libreoffice`。字体由对应机器提供。

完整分发包显式带转换器及其全部许可证；Windows 示例：

```powershell
python tools/package_app.py --bin-dir build/ci/bin --qt-bin C:/Qt/6.10.1/mingw_64/bin --output-dir dist/editorial-v11 --office-dir .tools/libreoffice --preview-fixtures runtime/office-fixtures
python tests/office_package_smoke.py --package-dir dist/editorial-v11
```

macOS `--office-dir /Applications/LibreOffice.app`。带引擎的包显著大于 PDF-only 包；`release-manifest.json` 记录 `office_preview_bundled`。默认不联网获取转换器，不将用户文件发送至在线转换服务。未带引擎的轻量包需依赖系统安装，不得称为零依赖的 Office 预览包。

## 可复现验收

测试依赖：`python-docx==1.2.0 python-pptx==1.0.2 openpyxl==3.1.5`。生成 Word 两页、16:9 PPT 两张、Excel 指定打印区域及分页两页，并导出三个旧二进制格式。中文、空格路径作为测试输入。

```powershell
python tools/generate_office_fixtures.py --office "$env:CPS_OFFICE_EXECUTABLE" --output-dir runtime/office-fixtures
$env:CPS_OFFICE_FIXTURES = (Resolve-Path 'runtime/office-fixtures').Path
$env:QT_QPA_PLATFORM = 'windows'
$env:CPS_GUI_EVIDENCE_DIR = (Join-Path (Get-Location) 'runtime/office-evidence')
& build/ci/bin/gui_tests.exe officeConversionLifecycle officeDocumentsRealPreview officeStudentPreviewAndReplacement previewPreservesMixedPageDirections editorialDocumentPreview -o runtime/office-tests.txt,txt
```

`officeConversionLifecycle` 使用未分发的进程测试替身，覆盖响应性、缺失组件、取消、替换、清理及失败；这不能替代真实导入。`officeDocumentsRealPreview` 对六种真实格式逐个检查两页、首末页真实渲染、方向及原件不变。`officeStudentPreviewAndReplacement` 检查 GUI 页数、阅读器、手动旋转、移除与快速替换。混合 PDF 用例不依赖 Office。

CI 安装真实 LibreOffice、生成六格式输入，在 Windows/macOS/Linux 执行真实导入；Windows/macOS 另外保存原生窗口截图，打包完整引擎。在第二台 runner 解压、校验 manifest，使用包内引擎和渲染器测试六格式；SDK 路径及外部转换器配置均被移除。

部署诊断命令不会启动后台或显示窗口：`campus-print --check-preview <文件> --preview-report <JSON路径>`；成功返回 0，报告含实际页数、页面尺寸、首末页渲染结果。失败返回 65（报告写入失败为 74）。独立包验证必须成功渲染真实页，不能只检查 exe 存在或转换 PDF 存在。

## 验证记录

2026-10-03：Windows 11 / Qt 6.10.1 / MinGW 13.1 / 私有 LibreOffice 26.8.0：最终原生窗口测试 13/13 通过，0 失败、0 跳过，耗时 159.439 秒。覆盖进程生命周期、六格式真实导入、学生端替换/移除、16:9 横向 PPT、四次旋转恢复、混合方向 PDF、既有 PDF/图片/文本预览和悬停展开回收。证据位于被忽略的 `runtime/office-final-tests.txt` 与 `runtime/office-final-evidence/`；PPT 桌面及阅读器截图已检查。远端和独立发布包结果以当前提交的 Actions job 为准，未执行的 macOS 验收不得写成已通过。

接口静态复核：同日通过 GitHub API 读取共同仓库 main `b55acd8` 与前端 `64df221`，未拉取共同主分支、未运行其后台。请求名称、支付字段及 schema 保持兼容；主分支新增可选 `forecast_at_ms` 及等待预测修复，合并时保留该提交及其 GUI 适配。此静态结论不是双端运行验收。

发布前增量回归：生命周期、混合方向 PDF 与默认收款资源/本地覆盖用例 5/5 通过。两张用户授权公开的原图已编入资源，文件哈希与现有本地配置一致；真实转账和到账确认没有测试。

参考：[LibreOffice 参数文档](https://help.libreoffice.org/latest/en-US/text/shared/guide/start_parameters.html)、[issue #1](https://github.com/korwave/campus-print-scheduler/issues/1)。
