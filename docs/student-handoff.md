# 学生端代理交接与部署

Status: implemented; Windows local validation recorded below; remote CI is authoritative for other platforms.
Scope: Qt Widgets 学生客户端「纸间」，分支 `feat/print-editorial`。本文件是当前实现与部署的入口；设计迭代证据见 `editorial-frontend.md`，经营协议见 `interfaces.md`。

## 接手顺序与完成条件

1. 在实际 Git 根目录执行 `git status --short`、`git branch -vv`、`git remote -v`。保留已有修改，使用任务指定分支。完成条件：确定待修改文件、提交目标和运行数据目录。
2. 按下表定位模块。学生端沿用现有 controller/coordinator 协议，业务元数据与文档图像分开处理。完成条件：变更涉及的行为与协议边界都有对应入口。
3. 执行相关功能测试；涉及打包、窗口或子进程时执行原生与独立包检查。完成条件：命令成功退出，实际输出支持所报告的行为；跨平台结果取自对应 CI job。
4. 部署时先验证包，再更新本机启动入口。完成条件：启动到预期版本，数据路径明确，源码和通用发布包包含本文件及依赖许可证。

## 模块定位

| 入口 | 责任与修改边界 |
| --- | --- |
| `src/gui/main.cpp` | `--role student` / `boss`，`--data-dir`，`--document`，品牌资源初始化；默认角色仍为老板端 |
| `src/gui/student_window.{hpp,cpp}` | 三页导航、打印规格、报价、模拟支付、订单与队列；`updateProof()` 同步设置，支付中锁定方式与关闭操作 |
| `src/gui/print_proof.hpp` | 桌面、纸叠位置、悬停区域、展开保持、单双面与页序、放大阅读器、图层缓存 |
| `src/gui/paper_transition.hpp` | 展开 Hermite 插值、反向时继承速度、端点约束；已有曲线与时长由用户验收 |
| `src/gui/document_preview.{hpp,cpp}` | GUI 进程内读取 PDF/图片/UTF-8 文本，动态加载 PDFium C ABI；源字节存活至文档关闭，调用在 GUI 线程 |
| `src/gui/office_preview.{hpp,cpp}`、`docs/office-preview.md` | Office 本地异步转换、独立进程及临时目录、取消清理、完整引擎打包和六格式真实验收 |
| `src/gui/editorial_chrome.{hpp,cpp}` | 无系统标题栏、主题窗口按钮、拖动与边缘调整、Windows 最大化工作区、对话框公共样式 |
| `src/gui/editorial_surface.hpp` | 主页面背景；打印设置竖分隔线仅在下单页绘制 |
| `src/gui/editorial_color_switch.hpp` | 半透明黑白/彩色切换条，保留独立键盘焦点与原 checkbox 同步 |
| `src/gui/ui_theme.hpp` | 学生表格主题参数；老板端现有主题独立保留 |
| `src/gui/payment_icon.hpp` | 支付方式品牌矢量图标 |
| `src/gui/payment_receipt.hpp` | 本地收款图片加载、缺失/损坏占位、方式切换、无系统标题栏放大展示 |
| `include/cps/platform/process.hpp` | Qt 子进程的 Windows 无可见控制台启动；coordinator / supervisor / controller 调用，其他平台保持 Qt 默认行为 |
| `assets/editorial/`、`CMakeLists.txt` | Qt 资源、Windows ICO/RC、PDFium 库定位与复制 |
| `tools/setup_pdfium.py` | 固定版本 PDFium 及许可证安装到工作树 `.tools/pdfium` |
| `tools/package_app.py`、`tests/package_smoke.py` | 匹配 Qt 部署、文件清单、第三方许可证、无 SDK 独立启动与后台打印验证 |

## 已验收行为

- 视觉：白色纸张、朱红强调、墨蓝文字、暖灰背景。订单进度/取件码/完成及空闲状态使用学生主题；表格选中行为和业务状态保持原语义。
- 窗口：Windows GUI 子系统及品牌 ICO；品牌、导航、学号与窗口按钮共用顶部一行；后台进程可继续执行且无可见控制台。
- 纸叠：悬停自动展开，离开复位；点击展开固定姿态，工具按钮区域保持悬停；减少动效停下动画并落到最终状态。翻页保持单双面映射，奇数末张背面留白。
- 预览：替换/移除文档或变更相关规格关闭旧阅读器；支持适合整页、125%、滚动和键盘翻页。Word、PPT、Excel 由本地 LibreOffice 异步转换后真实预览；部署须按 `office-preview.md` 提供引擎。横向页面自动适配，第四个工具按钮只旋转预览。
- 文档：文件上限 64 MiB，PDF 1–1000 页；文档内容仅由 GUI 读取，后端接收订单元数据。上传入口在本项目表示选择本地文件。
- 支付：三个支付品牌图标，微信/支付宝收款图片及放大展示，校园卡终端占位。图片仅作展示，支付、订单状态与打印输出仍由模拟后端驱动。

真实码扫码可能发生真实转账；应用内“确认支付”不证明到账。接入真实支付必须另行实现订单关联、金额校验、可信服务端支付结果/回调及幂等处理，再调整当前提示和订单状态。以用户点击或展示二维码作为付款成功依据会破坏此边界。

## 动画性能维护

保留预合成纸张/阴影、按窗口尺寸与 DPR 缓存的背景，以及不透明预览区。动画每帧主要变换图层；PDF 渲染、整页灰度转换与多层阴影生成发生在缓存构建时。放大阅读器读取原始页面。

缓存口径：纸张图层按 KiB 计费，上限 32 MiB；示例页面最多 8 页；文档渲染最多 12 页。后两者按条目计费，32 MiB 不是整个程序的内存上限。 decoded 源图片、窗口 backing store、PDFium 与 Qt 也占内存。代码没有因动画逐帧读写磁盘或播放声音；硬件滋滋声原因没有组件级验证。

完整展开行程 240ms，部分行程 120–240ms；中途改变目标保持当前位置和速度，边界限定 [0,1]。优化时先测量再改变缓存/绘制路径，保持用户已接受的时长、曲线与减少动效行为。

## 收款配置

配置目录是实际 `--data-dir` 下的 `payment-codes/`，格式如下：

```text
<data-dir>/payment-codes/wechat.png  或 wechat.jpg
<data-dir>/payment-codes/alipay.png  或 alipay.jpg
```

同名 PNG 优先；若 PNG 存在但损坏，显示未配置状态，不隐式退到 JPEG。换格式时移走过期同名 PNG。单文件不超过 8 MiB，宽高各不超过 4096。保留整张图比例与二维码留白。替换后重新选择支付方式或重新打开支付窗口刷新。

源码根目录 VBS 使用 `runtime/editorial-shop`；发布包 `start-student.vbs` 使用 Qt 用户数据目录。两者不是同一个目录。共享老板/学生状态时向两端传入相同的绝对 `--data-dir`。用户已授权两张真实收款码公开提交，原图位于 `assets/editorial/payment-codes/` 并编入 Qt resources，发布包自带默认展示。数据目录中对应方式的 PNG/JPEG 优先；文件存在但损坏时显示配置错误，不退到默认收款人。换收款人必须分别覆盖两种方式。真实到账仍未接入。

## 从源码构建

Python >= 3.10、CMake >= 3.24、Ninja、C++17 编译器及匹配 ABI 的 Qt 6 Widgets/Network/Test。Windows MSVC Qt 与 MinGW Qt 分别配套对应编译器；测试与部署使用构建所用的同一套 Qt。CI 的 Qt 版本以 `.github/workflows/ci.yml` 为准。

从仓库根目录执行，Windows 先将匹配编译器和 Qt bin 加入当前会话 PATH：

```sh
python tools/setup_pdfium.py
cmake --preset ci -DCMAKE_PREFIX_PATH="<Qt安装目录>"
cmake --build --preset ci --parallel 2
ctest --preset ci --output-on-failure
```

`ci` 为 Release；`dev` 为 Debug。PDFium 安装要先于 configure，以便 CMake 定位库并复制到 GUI/测试旁。初次缺失库后，应安装并重新 configure/build。可通过 `CPS_PDFIUM_LIBRARY` 定位已有库，打包仍须配套许可证目录。

## 原生 GUI 检查

Windows PowerShell（macOS 将平台改为 `cocoa`，去除 `.exe`）：

```powershell
$env:QT_QPA_PLATFORM = 'windows'
$env:CPS_GUI_EVIDENCE_DIR = (Join-Path (Get-Location) 'runtime/native-gui-evidence')
& ./build/ci/bin/gui_tests.exe editorialMotionRetargeting editorialAnimationFrameBudget editorialReceiptCodes editorialHoverSpreadsAndReturns editorialWindowChrome editorialDocumentPreview studentOrderPaymentAndForecast -o runtime/native-gui-tests.txt,txt
```

性能测试默认输出耗时，不绑定 CI 硬件速度；在原生本机验收设置 `CPS_GUI_VERIFY_FRAME_BUDGET=1` 启用绘制 p95 < 16.7ms、帧间隔 p95 < 27ms 的门槛。QtTest 使用窗口系统路径注入鼠标位置，避免共享桌面光标造成偶发失败。测试截图需要实际渲染，不能用设计图替代。

性能采样至少执行 3 轮开合；若 CI 原生显示合并绘制事件，继续采集至至少 12 个真实帧间隔，最多 12 轮。未采足仍失败；此采样策略不改变本机验收门槛或生产动画。CI 虚拟桌面的时间只作为该环境观测，不能推断所有实体 Mac 的流畅度。

性能用例直接驱动展开目标，在该用例的预览组件上关闭自动 hover 事件，防止操作者真实鼠标在窗口出现时改写动画目标；生产代码保持 hover，专门的 `editorialHoverSpreadsAndReturns` 验证完整悬停行为。原生 CI 日志同时写入标准输出与 evidence 文件，失败时可直接定位用例。

`editorialReceiptCodes` 检查默认资源能读取、本地配置优先、损坏配置不能退回默认收款人，并用临时色块检查缺失、比例和放大对话框。`CPS_GUI_RECEIPT_DIRECTORY` 可额外读取本地配置并保存截图；截图在被忽略的 `runtime/`。默认真实码已经包含在源码及发布资源，不能将它描述为未公开。

Windows 清理测试目录时，coordinator 的锁文件消失先于操作系统释放重定向日志句柄。`process_integration.py` 对本次临时目录的 WinError 32 做最多 5 秒的释放等待；其他错误立即失败，超过期限仍失败。不能用锁文件消失推断所有文件句柄已经关闭。

macOS 的 `QDialog::open()` 对尚未明确设置 WindowModal 的对话框使用 `setParent(..., Qt::Sheet)`，重置原窗口 flags。文档阅读器在 `open()` 前明确设置 `Qt::WindowModal`，与支付、收款码窗口一致；保留非阻塞打开方式及无系统标题栏要求。参见 [Qt 6.10.2 源码](https://github.com/qt/qtbase/blob/v6.10.2/src/widgets/dialogs/qdialog.cpp#L471-L486)。跨平台回归同时检查窗口 flags 和 modality。

## 发布包与启动

提交与构建完成后，选一个尚不存在的输出目录，保证 manifest 对应交付提交：

```sh
python tools/package_app.py --bin-dir build/ci/bin --qt-bin "<Qt安装目录>/bin" --output-dir dist/editorial-v10
python tests/package_smoke.py --package-dir dist/editorial-v10
```

分发整个目录/ZIP（Windows）或 `.app` 与同包材料（macOS），含 GUI、coordinator、supervisor、worker、回放工具、匹配 Qt plugins、PDFium 与许可证。脚本拒绝覆盖旧输出。`release-manifest.json` 包含提交、工作树状态和文件 SHA-256；有效交付要求提交匹配、工作树 clean，并且 smoke 检查通过。macOS 使用 ad-hoc 签名，不等于正式 notarization。

包内携带 `README-release.md`、本文件、设计验证说明和清理说明。Windows 双击 `start-student.vbs` 只显示学生 UI；运行学生端需显式 `--role student`，直接启动 EXE 默认老板端。显式配置共享数据的例子：

```powershell
& ./campus-print.exe --role student --data-dir 'D:/PrintShop/runtime'
& ./campus-print.exe --role boss --data-dir 'D:/PrintShop/runtime'
```

源码根目录 `start-editorial.vbs` 自动选择列出的最新本地包，再回退 `build/dev/bin`，使用工作树内数据。桌面快捷方式应指向该 VBS，通过 `wscript.exe` 运行并使用品牌 ICO；快捷方式为机器本地配置，Git 克隆不会创建它。`start-editorial.cmd` 是命令行兼容入口。

关闭 GUI 后后台保持运行。删除/更换包前按 `clean-up.md` 向对应数据目录发送 shutdown，并确认该后台退出；只管理此目录的进程。

## 验证证据与限制

2026-10-02，Windows 11 / Qt 6.10.1 / MinGW 13.1 的 v8 Debug 本地完整 CTest 13/13、原生收款码/窗口/支付用例、独立包无 SDK 启动与模拟打印均通过。两张实际 JPEG 读取与放大展示通过；没有执行真实付款或自动到账验收。

v7 同机原生测量：纸张 paint p95 约 4.5ms，完整窗口示例与黑白 PDF 的帧间隔 p95 约 17ms。这是指定窗口尺寸及缩放下的本机样本，不是所有机器的保证；详细条件见 `editorial-frontend.md`。

2026-10-02，`ci` Release / Qt 6.10.1 / MinGW 13.1 本机完整 CTest 13/13、带帧预算门槛的原生 GUI 9/9 通过。Release 绘制 p95 4.44ms，完整窗口示例/黑白 PDF 帧间隔 p95 17.13/17.08ms。Windows 临时目录日志释放竞态已修正，进程集成测试连续三轮及最终完整套件通过。

远端三平台结果以本次提交对应的 Actions job 输出为准。真实打印机、真实支付确认、学号身份认证及陌生机器现场操作尚未实现或验收；维护时保留其明确模拟边界。
