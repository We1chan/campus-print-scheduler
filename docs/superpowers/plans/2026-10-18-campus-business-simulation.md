# 校园经营仿真实施计划

**目标：** 在现有调度工作台保留四个教学场景，增加固定种子、六台异构设备、150/300/600 单的八小时经营对比。

**架构：** 扩展 merchant_dispatch_model，订单统一调用 quote，三策略统一调用 replayShop，评价统一调用 shopQuality。工作台只操作内存副本，不发送订单或改变真实后台策略。沿用现有 Qt Painter、按钮与场景选择器，不新增依赖。

**数据约定：** 固定种子 20261018；订单包括作业、报告、讲义、表格、彩色材料、A3 图纸等模拟业务，前四小时持续到达；六设备使用 defaultFleet(1250)，八小时闭店时统计。不得筛选种子或改变权重保证某策略获胜。未完成订单进入等待观测与快速单准时率的分母，闭店后完成不能结算。

- [x] 扩展 tests/merchant_dispatch_tests.cpp：先检查七个场景及三档订单规模，运行并确认当前实现失败。
- [x] 扩展 src/gui/merchant_dispatch_model.hpp/.cpp：确定性业务负载、六设备回放、固定口径指标、相对基线说明、包含完整输入/设备/三策略派发与指标的 JSON 导出。
- [x] 扩展 src/gui/merchant_dispatch_painter.cpp/.hpp：六设备并行轨道、平均/P95 等待、快速履约、完成数、利润、综合分；订单分页和选中跨策略高亮；回放状态与最终结果分开标识。
- [x] 修改 src/gui/merchant_window.cpp：场景切换、经营时钟播放、分页、导出文件对话框、演示截图入口。保留现有教学交互与真实后台协议。
- [x] 检查输入公平性、重复生成一致性、设备兼容与无重叠、闭店截断、指标可由轨迹重算、导出可复核；运行针对性的 CTest。
- [x] 构建最新店主端，保存实际渲染截图，打开新增场景供用户查看；更新 docs/merchant-frontend.md 的功能和模拟边界。

**验收命令：** `cmake --build --preset ci --target campus-print-merchant merchant_dispatch_tests merchant_tests --parallel 2`，配置匹配 Qt/MinGW PATH 后执行 `ctest --test-dir build/ci -R "^merchant(_dispatch)?$" --output-on-failure`。

## 已完成验证

- 首次新增七场景断言失败，补实现后专项测试通过。
- CTest `merchant` 与 `merchant_dispatch` 2/2 通过；最后的按钮调整又通过 Windows 原生 `nativeDispatchWorkspaceIsInteractiveAndIsolated` 和完整调度模型专项。
- 150/300/600 单 JSON 经 `tools/audit_dispatch_simulation.py` 独立审计通过；篡改分数和输入哈希均被拒绝。
- 报告位于忽略目录 `runtime/business-simulation-20261018/simulation-{150,300,600}.json`；原生截图位于同目录 `tests-native/`。
- 600 单实际结果：FCFS/SJF/Balanced 综合分 40.36/64.34/65.65，快速准时率 15.7%/77.6%/83.0%。Balanced 相对 SJF 平均等待增加 3.1 分钟、完成数少 3 单、贡献利润少 171.43 元；界面保留这些取舍。
- 独立代码审查无阻塞问题；分页宽度和输入指纹审计两项低优先级建议均已落实。

## 预览后的时间轴简化

用户反馈全日订单过密。已改成共同的 30 分钟局部窗口，每五分钟前移；每设备仅保留当前与后续最多三单，完成单移出，宽条显示订单号。左侧卡片暂停并定位所选策略下该单的开始时刻，完整指标和导出不变。

先增加可见订单数量限制，原界面断言失败，再完成局部视图。模型专项检查窗口边界、三单上限、真实派发记录保留、整场导出不受影响；Windows 原生交互检查通过，覆盖定位、播放与推进一小时。新截图和日志位于 `runtime/business-simulation-20261018/rolling-native/` 与 `rolling-native-test.txt`。

## 文字排版优化

根据后续截图反馈，把经营设备行距从 14 提高到 16、轨道增高到 13，并扩大设备名称与时间轴、时间轴与营业指标之间的留白。指标按名称、主数值、完成/利润、辅助信息分层，单位移入标签；底部改为固定列的基线对比表，选中订单时则显示三列策略执行信息。原生交互检查通过，950/1269/1586 三种窗口尺寸的截图保存在 `runtime/business-simulation-20261018/spacing-native/`，日志为 `spacing-native-test.txt`。
