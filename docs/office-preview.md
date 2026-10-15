# 学生网页的 Office 预览

学生端已统一为网页，Office 预览由 `tools/classroom_web.py` 的 `OfficeConverter` 和 `PdfEngine` 实现；原 Qt 学生预览代码与 `--check-preview` 诊断入口已移除。

## 链路与边界

浏览器上传 `.doc/.docx/.ppt/.pptx/.xls/.xlsx` → 网关保存私有原件 → 后台 LibreOffice 转换为 PDF → Python PDFium 读取页数、渲染 PNG → 浏览器展示。PDF、图片和文本沿用网关各自的预览路径。

一次只转换一份 Office 文档，排队容量 30，单次超时 90 秒。转换前复制到独立临时目录，并建立私有 `UserInstallation`，宏安全级别为 3；不修改原文件、不复用个人 Office 配置。超时时 Windows 停止转换进程树，POSIX 停止进程组。macOS 子进程设置 `PYTHONDONTWRITEBYTECODE=1`，避免改写上游已签名缓存。

Word 保留文档分页；PPT 保留幻灯片尺寸；Excel 使用 `calc_pdf_Export`，保留打印区域、分页和纸张缩放，不启用 `SinglePageSheets`。转换效果依赖 LibreOffice 导入和本机字体。

上传上限 10 MiB，转换 PDF 上限 64 MiB，PDF 上限 1000 页。转换成功后以真实 PDF 页数下单；未安装引擎时按网页现有提示允许手动填写页数。转换后的文件和原件只留在网关数据目录，不传入 coordinator；后台打印仍输出模拟文本。

## 依赖发现与部署

发现顺序：`CPS_OFFICE_EXECUTABLE` 显式绝对路径 → 发布目录 `office/` → 项目私有 `.tools/libreoffice` → 系统安装或 PATH。显式配置无效时不会静默改用其他引擎。

Windows 发布路径为 `office/program/soffice.com`；macOS 为发布根目录的 `office/LibreOffice.app/Contents/MacOS/soffice`，与 `CampusPrint.app` 分开。完整引擎和全部许可证必须一起分发，不能只复制 soffice。

```powershell
python tools/setup_pdfium.py
python tools/setup_office_preview.py
$env:CPS_OFFICE_EXECUTABLE = (Resolve-Path '.tools/libreoffice/program/soffice.com').Path
```

安装脚本沿用固定 LibreOffice 版本及 SHA-256 校验。Windows 生成私有行政镜像；macOS 复制私有 app，不覆盖系统安装。Linux 使用发行版 LibreOffice。

Windows 完整包示例：

```powershell
python tools/package_app.py --bin-dir build/ci/bin --qt-bin "<Qt目录>/bin" --output-dir dist/web-student --office-dir .tools/libreoffice --preview-fixtures runtime/office-fixtures
python tests/package_smoke.py --package-dir dist/web-student
python tests/office_web_preview.py --package-dir dist/web-student
```

macOS `--office-dir .tools/libreoffice/LibreOffice.app`；分发整个目录。打包用 `ditto` 保留转换器框架元数据、资源叉和符号链接，部署前后检查上游签名；Qt 只部署主 app，不接管转换器。网页 PDFium Python 模块及许可证保存在包内 `.tools/pdfium/`，网页运行仍需要 Python >= 3.10。

## 验收

生成六种真实输入后，使用当前网页预览引擎检查两页、首末页渲染、PPT 横向和原件哈希。HTTP、转换排队与错误处理由 `classroom_web_test.py` 覆盖。

```powershell
python -m pip install python-docx==1.2.0 python-pptx==1.0.2 openpyxl==3.1.5
python tools/generate_office_fixtures.py --office "$env:CPS_OFFICE_EXECUTABLE" --output-dir runtime/office-fixtures
$env:CPS_OFFICE_FIXTURES = (Resolve-Path 'runtime/office-fixtures').Path
python tests/office_web_preview.py --source-root .
ctest --preset ci --output-on-failure
```

未配置 `CPS_OFFICE_FIXTURES` 时，CTest 的 `office_web` 明确标为跳过；不能将替身测试或跳过用例写成六格式真实导入通过。CI 配置真实引擎与六格式输入，独立发布验收必须自动找到包内引擎并实际渲染，不能让系统安装掩盖部署缺失。macOS 六格式转换后再次检查上游签名。
