# 店主端验收记录

## v13 跨平台提交前修正（2026-10-09）

同步学生端预测回归的后台握手等待：先等暂停控件可用、再等暂停确认，随后才提交测试订单，避免慢启动时向尚未连接的后台发送操作。原有预测、恢复派发及队列断言不变。此项只改测试与说明；发布包运行代码与已提交的 v13 构建一致。

店主分支首次远端构建暴露了本机 MinGW 未报出的兼容性问题：Qt `qint64` 与 Linux `TimeMs` 的底层类型不同；MSVC 对 32 位移位结果升至 64 位给出警告；任务栏属性函数缺少 Windows SDK 的直接声明；新版 Clang 将旧的空可变参数 Qt 测试宏作为 C++20 扩展警告。分别统一演示时钟的 `TimeMs` 运算、使用 64 位移位、显式包含 `shellapi.h`，并沿用学生分支已有的 `QTest::qSkip` 调用。保留 C++17 与 warnings-as-errors，不降低检查要求，也不改变调度规则、界面和现有数据。

本机最终 Release 完整 CTest 15/15 及 Windows 原生菜单/调度/IPC 检查保留为 `runtime/merchant-evidence/v13-ctest.txt` 和 `v13-native-menu.txt`；最终本地运行包改为独立 `merchant-v13`，验收输出为 `v13-package-verification.txt`。两端 README 的功能表与调度工作台入口说明继续保留。远端各平台和独立运行检查以最终提交的 Actions 结果为准。

## v12 菜单阴影与分支功能说明（2026-10-09）

- Windows 原生菜单样式会在自绘阴影之外叠加深色阴影。弹出菜单改用局部 Fusion 基础样式，继续保留两端统一的纸白、墨蓝、淡红反馈和原生 QMenu 操作；自绘阴影减至 10 层、每层 alpha 2、下移 2px。主窗口系统阴影保持原效果。
- 回归检查整个面板外侧的透明区域，最大 alpha 不超过 24，防止只测远处淡影而漏掉紧贴边缘的深色阴影。实际两菜单截图见 [v12 菜单效果](images/merchant-menu-shadow-v12.png)。
- 本机 Release 完整 CTest 15/15 通过，Windows 原生调度工作台、店铺菜单和真实 IPC 三个场景全部通过（含初始化/清理为 5 passed）。日志位于 `runtime/merchant-evidence/v12-ctest.txt` 和 `v12-native-menu.txt`。
- README 开头列出店主端四页功能，并明确“打印队列 → 调度工作台”的按钮位置、五策略/四场景、播放与单步操作、返回方式及教学订单边界。调度工作台尚未接入后台实时订单。
- 本机新包使用独立 `dist/merchant-v12` 目录，保留旧版本及原 v1 真实店铺数据。仍采用完整可运行目录交付，不重复生成本机 ZIP；发布检查输出保留为 `v12-package-verification.txt`。

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

## v2 局部修复验收（2026-10-08）

- `ci` Release 增量构建通过，warnings-as-errors 保持开启；完整 CTest 14/14 通过。
- 新增三个界面回归：选中设备图片点击/鼠标焦点/键盘焦点后的静止图像一致，淡红悬停改变像素且移开恢复；窗口最大化/还原/最小化/恢复/关闭和 Space 激活；店铺菜单能触发工作台和刷新，并正确退出弹出事件循环。
- 三个新增场景额外在 Windows 原生平台运行：5 passed / 0 failed（含初始化和清理）。日志和截图在 `runtime/merchant-evidence/v2-native*`。
- 真实已运行队列的四页抓图在 `runtime/merchant-evidence/v2-real/`；两张单色卡的“未配置”均在分隔线左侧留出 16px。照片交互区域没有点击后持续红框。设备外沿保留当前选中态。
- 用户选择 A“门店招牌”。仅新增 `assets/merchant/brand-*.png` 和 `paperroom-merchant.ico`；学生端品牌 PNG/ICO、程序嵌入的原图标和学生启动脚本没有修改。
- 新增源码根目录静默启动器 `start-merchant.vbs`，优先 v2，并继续使用已经存在的 v1 店铺数据目录；桌面独立店主快捷方式引用专用图标。

v2 包生成后使用单独存放该 ZIP 的归档目录执行 `tools/verify_package.py`；该工具校验 manifest 全部哈希，并依次执行无 SDK 的 GUI/持久队列、完整 Office 六格式和进程控制验收。结果留在 `runtime/merchant-v2-package-check/`，保留 v1 包及其真实运行数据。

## v3 图标和字体统一验收（2026-10-08）

- 按学生端实际源码对齐字体角色：品牌、导航、栏目、表格和数据使用微软雅黑；四页大标语及纸面文档使用宋体。品牌为 24px 粗体，导航为 18px 常规，主标语为 60px 粗体。统计数值和单位按实际字体宽度排版。
- 用户选定 A 门店招牌后，新增 `assets/merchant/icons-v3/`，扩大门店标识。原学生品牌、v2 店主图标及旧发布包保留。
- Windows 新增独立 `campus-print-merchant.exe`，嵌入新版图标；原 `campus-print.exe` 保留学生图标。两者的七种尺寸嵌入图标已逐帧校验完整图像载荷。
- 修复共享窗口框架覆盖店主窗口图标的问题。在框架初始化后设置店主图标，并设置独立 AppUserModelID、重启命令和任务栏图标资源。原生测试直接读取窗口 IPropertyStore 核对这些属性，截图留在 `runtime/merchant-evidence/v3-native/`。
- `ci` Release 构建通过，完整 CTest 14/14 通过。最终 Windows 原生图标、悬停焦点、窗口控件及店铺菜单回归共 6 passed / 0 failed（含初始化和清理），没有属性设置警告；日志为 `runtime/merchant-evidence/v3-native-tests.txt`。
- 四页源程序原生抓图在 `runtime/merchant-evidence/v3-showcase/`。桌面启动器优先选择 v3 专用程序，继续使用既有 v1 店铺数据目录。

发布验收使用单独的 v3 归档目录，结果以运行时输出为准：

```powershell
python tools/verify_package.py --archive-dir runtime/merchant-v3-archive --output-dir runtime/merchant-v3-package-check
```

验证包括完整 manifest 哈希、移除 SDK PATH 后的两端原生启动及持久队列、包内 Office 六格式两页预览、进程控制；范围仍限于本机 Windows。

## v4 标题栏悬停布局修复（2026-10-08）

- 四个导航文字与各自悬停区域使用同一个矩形并居中绘制，当前页的下划线同步居中。
- 门店按钮的悬停区域从 205×52 缩小至 120×42，仅包住门店图标和名称；移除名称后的向下箭头，点击仍打开原有店铺菜单。
- `ci` Release 构建通过，Windows 原生菜单、淡红悬停恢复、窗口控件、任务栏图标四项回归共 6 passed / 0 failed（含初始化和清理）。新增两张实际悬停截图位于 `runtime/merchant-evidence/v4-native/native-header-{nav,shop}-hover.png`，供视觉核对。
- 发布为独立 `merchant-v4`，沿用 v3 图标与既有 v1 店铺数据；保留学生端和旧发布包。完整 CTest 与发布验收输出分别存放在 `runtime/merchant-evidence/v4-ctest.txt` 和 `v4-package-verification.txt`。

## v5 队列翻页和学生指标文字修复（2026-10-08）

- 队列设备组入口恢复为“上一页 / 下一页”，显示当前页码；首尾页不循环跳转，每页展示两台设备，换页重置设备内工单滚动位置。
- 原生真实 IPC 用例覆盖六个设备槽位的三页前进和返回，并核对首尾边界，随后继续执行原来的订单提交、打印和设备设置验证。日志与第一页/末页截图位于 `runtime/merchant-evidence/v5-*`。
- 学生端订单/排队页的三行指标原来使用固定字号和默认布局间距，而外层卡片会按窗口缩放，造成文字框被挤压。同步学生分支的局部响应式字号、边距和间距修复，保持字体家族、字重和文案。
- 学生回归检查空队列、暂停派发与可取件状态，在 920×640、1180×820、1420×888 三种尺寸核对两页实际字体行高/宽度与文字框并保存截图。独立学生分支已在 Windows 原生 DPR 1.25 / 1.6 通过该用例。

## v6 原生窗口边界与阴影（2026-10-08）

- 两端共享窗口框架接入 Windows DWM：保留自绘标题栏，恢复系统窗口轮廓、圆角和柔和的窗口外阴影。活动窗口使用低对比度浅灰轮廓，失焦后进一步淡化；最大化时移除扩展边距，继续按工作区避开任务栏。
- Windows 原生测试读取 DWM 非客户区渲染状态和圆角属性，并在自建纯色背景上捕获合成器实际桌面画面、检查窗口底部阴影像素。截图为 `runtime/merchant-evidence/v6-native/merchant-native-frame.png`，不是 QWidget 内部抓图；辅助窗口仅在验收期间置顶。
- 原生窗口最小化、最大化/还原、键盘操作与店主独立任务栏图标测试通过（6 passed / 0 failed，含初始化/清理）。学生分支同步相同实现，原生窗口、文档预览和指标卡回归通过（5 passed / 0 failed）。完整 Release CTest 结果见 `runtime/native-frame-ctest.txt`。
- 独立发布版本为 `merchant-v6`；数据仍使用既有 v1 的 `runtime/shop`。桌面入口、原图标和旧发布包保留。包内完整哈希、无 SDK 启动、六种 Office 格式和进程控制验证日志为 `runtime/merchant-evidence/v6-package-verification.txt`。
- 使用 [Microsoft DWM 窗口属性](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute) 和 [扩展系统窗口框架](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmextendframeintoclientarea)；Windows 11 的圆角/轮廓属性在旧版本系统上由 DWM 忽略。本机视觉效果已验证，其他系统结果以对应环境为准。

同版追加：设备页底部的当前选择、A4 纸张及黑色墨粉信息拉开上下两行间距，并增加底部留白。去除店主全域悬停的分层淡红阴影，导航和设备图片保持透明；普通按钮/主按钮仅保留与学生端相同的平面颜色与边框反馈。原生回归核对设备图片在悬停、鼠标/键盘焦点和移开后的完整像素保持一致，真实设备操作仍可点击。最终日志与设备页截图在 `runtime/merchant-evidence/v6-final-native*`。

## v7 全页面文字与交互一致性（2026-10-08）

- 以学生端 src/gui/student_window.cpp、editorial_surface.hpp、editorial_chrome.hpp 的角色为依据，核对导航、品牌、栏目/数据/辅助文字、底部摘要与营业栏、普通/主要/禁用按钮、选择态、菜单、弹窗及系统窗口控件。校正店主四页的字号、字重、正文和辅助色；保留品牌、纸张大标语、店主独立图标和状态语义。
- 顶部导航切换前后截取文字区域做像素一致性比较，排除红色下划线；hover 和设备图片焦点回归继续通过。原生导航/搜索/文件弹窗与独立图标检查通过。两份原生日志合计 10 passed / 0 failed（包括两次初始化/清理），位于 runtime/merchant-evidence/v7-{native,pages}-tests.txt。
- 在真实 IPC 六设备状态下检查三页前进与返回。三个设备管理按钮宽高、位置、对象均稳定；首尾点击提示可访问文本正确，并且不翻页、不隐藏。原有队列页、订单提交、打印和设备配置继续验证。首尾页面截图在 v7-native/native-devices-{first,last}-page.png。
- 最终 ci Release / CTest 输出保存在 runtime/merchant-evidence/v7-ctest.txt；四页的最终无测试提示截图位于 v7-gallery/。独立 merchant-v7 发布包验收校对提交号、全部文件 SHA-256、无 SDK 启动、持久队列/模拟打印、Office 六格式和进程控制，日志为 v7-package-verification.txt。
- 桌面店主启动器优先 v7，沿用既有 v1 店铺数据；学生 v13 发布包和入口、旧发布包、全部概念图保留。

## v8 底部对齐与间距（2026-10-08）

- 营业状态绿点下移 3px，按实际中文字形可见范围对齐中心；文字大小和位置保留。
- 四页底部操作按钮统一为 y=875、54px 高，设备设置与打印测试页使用同一顶线和底线，绘制与点击区域同步。
- 设备页当前选择、在线状态、纸张及墨粉摘要整体上移 12px，保留两行文字的间距，增加摘要与营业栏分隔线之间的留白。
- 按用户最新要求取消翻页边界提示；设备管理与打印队列共用页码按钮绘制和禁用态，第一页禁用上一页、最后一页禁用下一页，按钮仍始终可见。真实 IPC 回归核对禁用点击无提示、无后台反馈，三页前进/返回和固定几何布局仍正常。
- 修复工作台“查看全部设备”跳转漏处理：本地打开设备管理页，不再将 page 操作交给业务后台；真实模式回归核对零错误反馈。
- 门店菜单、自动调度子菜单和订单菜单使用透明圆角窗口遮罩，禁用系统弹出菜单阴影，移除右下角露出的直角；主程序窗口的 DWM 阴影保留。
- 本版沿用已有窗口轮廓、系统阴影、图标和数据目录。构建/CTest、四页原生抓图及发布包验收证据分别在 runtime/merchant-evidence/v8-build.txt、v8-ctest.txt、v8-gallery/ 和 v8-package-verification.txt。

## v9 顶部布局与素材融合（2026-10-08）

- 四页顶部纸面区域统一至 y=292；使用同一 heroMetric 绘制四列，标签 y=206、数字框 y=235，分隔线 x=205/431/666、y=211..267。四页主标语与副标题采用相同位置，统计页金额和单位按列宽测量，避免溢出。
- 日期选择条使用不透明白底和更清楚的灰色外轮廓；统计日期留在同一顶部区域内，选择、焦点与键盘操作沿用现有行为。
- 叶子和咖啡/夹子照片仅在渲染缓存边缘做透明渐隐，融合到当前纸张底纹；夹子取图范围扩大并避开淡出区域，完整保留物体及自然阴影。原素材图片不覆盖。
- 调度工作台本轮仅生成概念预览，未新增该页面或修改调度算法。
- 构建、完整 CTest、四页实际截图和发布检查证据分别为 runtime/merchant-evidence/v9-build.txt、v9-ctest.txt、v9-gallery/、v9-package-verification.txt。桌面启动器优先 v9，继续使用原有 v1 数据目录。

## v10 预览落地与连续摄影纸面（2026-10-09）

- 四个业务页面使用同一张完整的无文字摄影纸面；取消叶子、咖啡和夹子的矩形局部贴图，保留完整夹子和自然光影。原始概念资源保留，新素材为 `hero-paper-v10.png`；调度页独立素材为 `dispatch-paper-v10.png`。批准预览保存为 `docs/images/dispatch-approved.png`，实际截图为 `merchant-dispatch.png`，两者区分记录。
- 新增打印队列的调度工作台入口。时间轴、五种策略、固定订单色、解释区与顶部控制均按已批准布局实现；使用原生按钮及下拉框，支持四场景、播放暂停、单步、速度、重置、订单跨策略高亮及返回。持续保留学生端的字体角色、深色导航、红色动作和平面交互，原生窗口框架与店主专用图标继续有效。
- 演示模型直接调用 `replayShop`，不修改调度核心和真实后台协议。输入与输出均采用统一教学时钟，等待、期限、派发、完成、历史及指标通过独立测试逐项与原始回放核对。覆盖 SJF 平均等待改善、期限策略改善、老化使旧低优先级订单提前、未来到达不可提前派发、五策略相同负载与全部完成。
- Windows 原生测试在 950×594、1269×794、1586×992 检查控制区域没有越界；验证场景选择、单步推进、播放暂停、策略/订单选择、重置及返回；核对原有订单状态、设备打印量与等待数不变。原生窗口控件、DWM 圆角阴影和店铺菜单回归同时通过。证据在 `runtime/merchant-evidence/v10-native-tests.txt` 与 `v10-native/`。
- Release 构建和完整 15 项 CTest 全部通过，结果为 `runtime/merchant-evidence/v10-ctest.txt`；四页及新调度页原生抓图为 `v10-gallery/`。正式 `merchant-v10` 包从已提交且 clean 的工作树打包，继续沿用 v1 的持久数据目录。打包和独立发布验证输出分别保存在 `v10-package.txt`、`v10-package-verification.txt`；部署完成证据为 `v10-completion.json`。

## v11 门店与策略菜单阴影（2026-10-09）

- 门店菜单与策略子菜单使用相同的中性灰柔和圆角阴影，阴影绘制在透明窗口的留白内，保留圆角纸白面板，避免系统方形阴影露出直角。菜单内容位置及子菜单纸面边缘保持对齐，原有按钮悬停与主窗口 DWM 阴影不变。
- 原生回归同时检查菜单操作、圆角透明角点及面板之外存在低透明度阴影；截图为 `runtime/merchant-evidence/v11-native/native-menu-shadow-pair.png`。完整 CTest 结果记录于 `v11-ctest.txt`，原生结果为 `v11-native-menu.txt`。
- 发布采用独立的 `dist/merchant-v11` 可运行目录，从 clean 提交构建打包。沿用 v10 已验证的完整 Office 运行时，目录所有文件核对 release manifest，移除 SDK 路径后执行启动、持久队列、六格式预览和进程控制验证；为保留旧版本并节省本机磁盘，本次不重复生成 ZIP。日志为 `v11-package-verification.txt`，部署结果为 `v11-completion.json`。
- 调度工作台仍是独立教学演示；本轮只读取真实 `snapshot` 核实已有接口，没有把真实后台订单接入演示页面，也没有修改队列或后台策略。接口调查记录为 `runtime/merchant-evidence/v11-backend-readonly.json`：当前该数据目录为 0 个订单、6 个设备槽位。
