# 跨平台验证与 CI 额度

## 本次修正（2026-10-09）

上一轮三平台的完整 CTest 均通过，但 Windows 原生窗口截图检查失败；店主分支另有 macOS Office 发布签名问题。修正覆盖这两个具体问题及 Windows 证据日志占用；订单、支付、调度协议及已确认的界面设计保持原有行为。

- Windows 先检查应用的原生 frame style、DWM 非客户区绘制和圆角属性，再在自有不透明背景上同时拍摄应用和普通系统窗口。系统窗口有阴影而应用没有时继续失败；两者都没有时明确记录该桌面无法验证阴影像素，不将其写成视觉验收通过。窗口按钮和其他行为断言继续执行。本机视觉验收设置 `CPS_GUI_REQUIRE_NATIVE_SHADOW=1`，即使系统对照也没有阴影仍然失败；不会修改用户的系统视觉设置。
- QtTest 在 MSVC 上通过 CRT 打开报告文件，脱离 GUI 存活的后台可能继承文件句柄。测试初始化仅清除当前 QtTest 报告句柄的继承标志，并检查结果，避免后台占用 XML/TXT。业务日志、后台持久运行和生产启动代码不受影响。
- macOS Office 完整发布包使用独立转换器布局：完整的上游签名转换器放在 `CampusPrint.app` 之外，用 `ditto` 复制，部署前后验证签名。测试样例生成和实际转换均禁止 Python 写回字节码。独立包测试不设置转换器覆盖，要求自动找到包内引擎、真实渲染全部六格式，并在导入后再次验证上游签名。

`tests/package_layout_test.py` 在实际打包脚本与部署工具的边界复现嵌套框架被改写的问题。它验证部署布局与签名校验调用，不能代替真实 macOS 签名和运行验收；真实验收仍在 macOS 构建及独立运行 job 中执行。

## 可重复命令与证据

完整本机检查：`cmake --build --preset ci --parallel 2`、`ctest --preset ci`。Office 场景必须配置真实 `CPS_OFFICE_EXECUTABLE` 与六格式 `CPS_OFFICE_FIXTURES`，并检查 QtTest 没有跳过这些用例。

原生检查统一入口：

```powershell
$env:CPS_GUI_REQUIRE_NATIVE_SHADOW = '1'
python tools/run_native_gui_tests.py --bin-dir build/ci/bin --log-dir runtime/native-validation
```

该命令等待测试真正退出，输出完整 QtTest 文本，保留失败退出码。学生端运行完整原生场景；Windows 店主构建额外验证调度工作台、菜单、窗口控制及真实 IPC。截图仅捕获应用自己的组件或覆盖了采样区域的自有背景。

两分支本机记录分别保存在被忽略的 `runtime/ci-repair-*` 与 `runtime/merchant-evidence/ci-repair-*`。最终远端结果以这轮交付提交对应的 Actions run 为准：包括 Windows/macOS/Linux 构建和完整 CTest，Windows/macOS 原生场景、发布包及独立运行检查。本机 Windows 通过不等于 macOS/Linux 已验证。

额度策略：先汇总已有失败日志，在本机复现和修复后集中推送。后续若只修改验证文件、Linux 已通过且 Linux 运行代码未变，可用提交标记 `[desktop ci]` 仅补 Windows/macOS；普通推送和 PR 仍验证全部平台。不能用此标记跳过尚未验证的 Linux 运行代码。发布产物已经是 ZIP/TAR.GZ，上传时不再二次压缩；完整平台、签名和独立运行检查保留。

## 两端功能说明

本机最终 Release / Qt 6.10.1 / MinGW 13.1：学生完整 CTest 14/14，原生学生场景 21 passed、零失败、零跳过。应用及系统对照的实际阴影均通过严格像素检查，退出后 XML 报告可独占打开。店主工作树另通过完整 CTest 16/16 和原生专项 6 passed。

学生端三页、真实本地预览、订单与排队入口见 README 开头和 [学生端交接](student-handoff.md)。店主端功能和调度工作台位置见店主分支 README；此学生分支未引入店主端界面。

首轮远端的业务测试均通过；新增打包回归测试误把 macOS `/var` 与 `/private/var`、Windows 短路径与完整路径当成不同目录，阻止了后续发布验收。已统一临时根目录的真实路径，并用目录 junction 和 Windows 8.3 别名在本机先复现失败、再验证修正。补验证保留两端的原生场景、真实签名、打包及独立运行，不重复 Linux 构建；应用运行代码与首轮相同。

原生补验证还修正了日志脚本的中文输出编码，并以真正的子进程 stdout 在 cp1252 环境下验证 UTF-8 日志、两个测试程序的证据及失败退出码。`native_test_runner` 已加入 CTest，不能吞掉失败。

Windows 托管桌面的采样范围现在取自不透明背景与工作区的交集，按实际截图原点计算底部阴影采样点。仅截取被自有背景覆盖的像素；底部采样区没有空间时仍失败。`CPS_GUI_FRAME_CANVAS_SIZE=1024x768` 可在本机限定自有背景、复现小桌面约束，不改变系统分辨率。应用和系统对照的实际阴影仍须满足原断言；截图结束后等待原窗口重新激活、恢复布局，再验证控件点击。

崩溃恢复用例分别等待新协调器 PID 出现、终态对账完成及 GUI 刷新，保留取消、完成、输出文件和尝试次数断言。原有单个 20 秒等待混合了重连与对账，现按真实异步阶段同步。本机另单独重复验证恢复场景；完整远端结果以对应 Actions run 为准。

最后一轮本机完整 Release CTest：店主 17/17、学生 15/15；新增 `native_test_runner` 均通过。小桌面原生检查保留严格阴影像素断言；最后的远端发布验收尚待对应 Actions 完成，不能以本机结果代替。

最后原生验收使用 1024×768 自有背景和 `CPS_GUI_REQUIRE_NATIVE_SHADOW=1`，同时将启动脚本 stdout 设为 cp1252：两分支公共原生用例均 21 passed，店主专项另 6 passed，均零失败、零跳过；应用与系统对照阴影均实测存在。
