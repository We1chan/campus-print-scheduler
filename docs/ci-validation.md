# 跨平台验证与 CI 额度

## 本次修正（2026-10-09）

上一轮三平台的完整 CTest 均通过，但 Windows 原生窗口截图检查失败，店主分支的 macOS 发布包还存在签名失败。修正覆盖这两个具体问题及 Windows 证据日志占用；订单、支付、调度协议及已确认的界面设计保持原有行为。

- Windows 先检查应用的原生 frame style、DWM 非客户区绘制和圆角属性，再在自有不透明背景上同时拍摄应用和普通系统窗口。系统窗口有阴影而应用没有时继续失败；两者都没有时明确记录该桌面无法验证阴影像素，不将其写成视觉验收通过。窗口按钮和其他行为断言继续执行。本机视觉验收设置 `CPS_GUI_REQUIRE_NATIVE_SHADOW=1`，即使系统对照也没有阴影仍然失败；不会修改用户的系统视觉设置。
- QtTest 在 MSVC 上通过 CRT 打开报告文件，脱离 GUI 存活的后台可能继承文件句柄。测试初始化仅清除当前 QtTest 报告句柄的继承标志，并检查结果，避免后台占用 XML/TXT。业务日志、后台持久运行和生产启动代码不受影响。
- 店主端沿用学生端已验证的 macOS Office 布局：完整的上游签名转换器放在 `CampusPrint.app` 之外，用 `ditto` 复制，部署前后验证签名。测试样例生成和实际转换均禁止 Python 写回字节码。独立包测试不设置转换器覆盖，要求自动找到包内引擎、真实渲染全部六格式，并在导入后再次验证上游签名。

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

额度策略：先汇总已有失败日志，在本机复现和修复后集中推送，每个分支只启动一轮新的 CI，不重跑旧失败。发布产物已经是 ZIP/TAR.GZ，上传时不再二次压缩；完整平台、签名和独立运行检查保留。

## 店主端功能入口

本机最终 Release / Qt 6.10.1 / MinGW 13.1：店主完整 CTest 16/16、学生完整 CTest 14/14；原生学生场景 21 passed、店主专项 6 passed，均零失败、零跳过。两端实际阴影及系统对照均通过严格像素检查。测试退出后三个 XML 报告均能独占打开。

调度工作台位于「打印队列」（Ctrl+2）标题同行，按钮在设备分页左侧。它展示五种策略、四种教学场景、同一时间尺度的任务轨迹，并支持播放、暂停、单步、倍速、重置与返回。教学样例没有接管后台实时队列。四个营业页面、其他功能及协议边界见 [店主端说明](merchant-frontend.md) 和 README 开头的功能表。
