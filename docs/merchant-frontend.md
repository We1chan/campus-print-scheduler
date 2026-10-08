# 纸间店主端

分支 `feat/merchant-editorial`，独立工作树 `campus-print-merchant`，从学生端提交 `42f77a1` 建立。学生端原工作树与其未提交概念图文件均保留。

## 打开

Windows 发布包中双击 `preview-merchant.vbs` 查看与概念图对应的交互样例；双击 `start-merchant.vbs` 连接真实持久队列。`start-student.vbs` 与真实店主端共同使用包内 `runtime/shop`，两端可以同时运行。样例完全独立，不读取或写入真实队列。

本机桌面的“纸间 · 店主工作台”通过源码根目录 `start-merchant.vbs` 启动最新的 `merchant-v5` 发布包，兼容 v4/v3/v2/v1。v5 沿用 v3 的专用 `campus-print-merchant.exe` 和加大门店角标的图标；学生启动脚本继续使用原 `campus-print.exe`。已运行过 v1 时继续使用其 `runtime/shop` 队列；全新安装使用本工作树的 `runtime/merchant-shop`。两个数据目录均需要保留。现有学生快捷方式、品牌 PNG/ICO 和旧店主图标均保留，新版图标另存于 `assets/merchant/icons-v3/`，发布包另带 `paperroom-merchant-v3.ico`。

源码构建后的等价命令：

```powershell
build/ci/bin/campus-print-merchant.exe --merchant-showcase
build/ci/bin/campus-print-merchant.exe --data-dir runtime/shop
build/ci/bin/campus-print.exe --role student --data-dir runtime/shop
```

开发启动须配置对应 Qt/MinGW DLL 的 PATH，发布包已包含运行库。旧老板控制台使用 `--classic-owner`；原有 legacy、demo-workload 入口仍保留。

## 页面和交互

四个顶栏页面分别为订单工作台、打印队列、设备管理、营业统计。`Ctrl+1…4` 切换，`Ctrl+F` 搜索，`Ctrl+R` 刷新。右上角最小化、最大化/还原、关闭按钮与学生端共用窗口控件。店铺菜单沿用纸白、墨蓝和淡红配色，提供后台刷新，以及真实模式的自动调度策略和全局暂停派发。

页面交互区域仅在鼠标悬停或按下时显示淡红阴影，点击后移开不保留焦点红框；键盘仍可聚焦并激活。设备卡外沿的选中态仍表示当前设备。单色设备耗材值距竖线保留 16px，避免“未配置”与竖线重叠。

字体分工与学生端统一：品牌为 24px 粗体微软雅黑，导航为 18px 常规微软雅黑，栏目和数据采用微软雅黑并用字重区分层级；60px 宋体仅用于页面大标语，文档封面也采用宋体。原生菜单与对话框继承 Microsoft YaHei UI。四页标语的红字按前半句真实字宽接续，数字和单位按实际字体宽度排布。

Windows 店主窗口使用独立 `Paperroom.Merchant` 身份和新版窗口图标。任务栏重新启动信息保留店铺数据目录，店主专用 EXE 内嵌新图标；共用窗口控件初始化后重新设置店主图标，避免被学生品牌覆盖。该机制遵循 [Microsoft AppUserModelID 说明](https://learn.microsoft.com/en-us/windows/win32/shell/appids) 和 [任务栏图标属性](https://learn.microsoft.com/en-us/windows/win32/properties/props-system-appusermodel-relaunchiconresource)。

工作台可按状态筛选、搜索文件名/订单号/学号、分页、选择订单和查看详情。右键订单可以取消等待任务；样例打印完成后可确认取件。队列按设备显示进度与顺序，较长队列可翻页。设备页可切换设备组、调整名称和能力、开关接单，打开耗材补纸窗口。统计页可按今日、本周、本月或自定义日期重算并导出 JSON 报表。

样例初始状态与确认的概念图保持一致：48 单，6 单等待，1 单打印中，12 单待取件，29 单完成；营业额 ¥326.80，524 页（黑白 428、彩色 96）。打开样例不会自行推进；按开始打印后才模拟对应设备，暂停、转移、兼容性校验、重排、添加设备及测试页会更新四个页面的数据。样例吞吐估算为每分钟 9 页，初始队列预测 12 分钟。

## 已有后台边界

真实模式接入现有 controller/coordinator：接收学生订单、观察打印进度和持久历史、调整设备能力/在线状态、切换自动调度策略、暂停派发、取消等待任务、补充共享库存、导出真实报表。完成时间和队列清空时间读取后台预测，暂停或受阻的未知预测不显示猜测分钟数。统计范围会同步显示对应的标签与日期。后台提供六个固定设备槽位；“添加设备”在真实模式中打开槽位配置。

现有协议没有手动指定设备、手动重排、指定单机测试页、取件登记或每台设备耗材遥测。这些操作只在标明的样例模式演示，真实模式禁用对应控件或显示未知值。停止设备接单不会中断已经开始的任务。真实模式的报表导出使用后台完整历史，界面的统计范围用于本地汇总。

学生提交的是文档元数据，源文件字节没有经过 IPC 传给店主。“查看文件”显示这个边界，并提供选择本机文件核对的入口；概念图中的报告封面仅是样例插画。物理打印和付款确认仍遵循原项目的模拟边界。

## 视觉验收

资源中的四张已确认概念图是视觉基准，参考坐标 1586×992。照片、纸纹和样例文档封面沿用原图素材；导航、文字、表格、图表、进度和按钮由 Qt 根据数据绘制。实际有原生 QPushButton、QLineEdit 与键盘焦点，不以整页图片充当界面。

以下为 Windows 原生 Qt 渲染截图：

![订单工作台](images/merchant-workbench.png)

![打印队列](images/merchant-queue.png)

![设备管理](images/merchant-devices.png)

![营业统计](images/merchant-stats.png)

具体测试、包验收记录见 [merchant-validation.md](merchant-validation.md)。
