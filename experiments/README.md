# A：可复现调度实验

2026-10-10 起，当前脚本比较 FCFS/SJF/Balanced；两层规则见 [调度说明](../docs/scheduling.md)。已提交的 2026-09-23 摘要和图表属于旧 PriorityAging 版本，保留原数值，复现旧结果需使用当时的程序和脚本。重新运行须使用新的输出目录。

已执行结果：[2026-09-23 实验报告](../docs/experiments-a.md)、[摘要](results/2026-09-23/summary.json)、[证据清单](results/2026-09-23/manifest.json)。共 33 组策略结果，每组重复两遍核对，四组图表同时提供 PNG / SVG。

`config.json` 固定数据来源哈希、3 个不重叠的 1000 条有效记录窗口、两台打印机和映射参数。
运行生产版 `convert_swf.py` 与 `scheduler-replay`；实验脚本独立从逐条派发记录重新计算指标，检查提前执行、重复任务、打印机重叠和工作量守恒。
每个场景执行两次，要求 JSON 报告字节一致。无随机采样，重复执行用于验证确定性，不作为独立统计样本。

## 运行

在项目根目录先按开发文档构建 `scheduler-replay`。以下使用 macOS/Linux 命令；Windows 将 `python3` 换成 `python`，可执行文件增加 `.exe`。

```sh
python3 tools/run_experiments.py --replay build/dev/bin/scheduler-replay --swf runtime/data/sdsc_sp2.swf --output runtime/a-experiments
```

脚本拒绝覆盖已存在的输出目录，也拒绝 SHA-256 与配置不符的源文件。尚无源日志时，下载 [WRENCH 镜像](https://raw.githubusercontent.com/wrench-project/task_clustering_batch_datasets/master/workloads/sdsc_sp2.swf)，保存为上述路径；下载失败不能把 HTML 当 SWF。

输出包含所有转换后输入、完整逐任务报告、重复回放报告、摘要和 manifest。Manifest 记录 UTC 执行时间、Git 提交、平台、程序/源码哈希、参数、实际命令和产物哈希。重新转换时元数据中的时间不同，因此另计算忽略转换/下载日期后的 workload_content_sha256；复现对比该哈希、各任务调度结果和统计值。

## 画图

绘图依赖只影响图表生成，不影响程序构建或 CTest。

```sh
python3 -m venv .venv
# macOS/Linux 使用 .venv/bin/python；Windows 使用 .venv/Scripts/python.exe
.venv/bin/python -m pip install -r experiments/requirements.txt
.venv/bin/python tools/plot_experiments.py --run runtime/a-experiments --output runtime/a-experiments/figures
```

生成 PNG 与可编辑 SVG：3 个窗口的平均/最长等待、等待分布、负载密度敏感性、老化间隔对低优先级大任务的影响。图中使用英文标签以避免各系统缺少中文字体，中文解释见实验报告。

## 实验设计和范围

- 基线：offset 0/1000/2000，每窗 1000 条；uniform 和 job-id-modulo 两种历史元数据映射；每个输入比较 FCFS/SJF/Balanced。当前 priority 不参与排序，两个映射不应作为不同业务优先级实验解释。
- 密度：第一个窗口固定页数、优先级，仅把到达时间缩放改为 0.002 / 0.01 / 0.05。
- 老化：自建 41 任务的有限压力场景，比较 1 / 30 / 120 秒老化间隔。这里只观察延迟，不能由有限样本宣称证明无限到达下不会饥饿。
- 每个场景固定 2 台同速打印机、150 ms/页。没有故障、启动耗时、换纸、双面计费或物理打印。
- summary 的 p95 使用 nearest-rank，即有序等待值的第 ceil(0.95*n) 项。
- status=1、正运行时长和页数截断会改变总体分布；不能将结果外推为校园真实需求，也不能凭单个窗口断言某算法普遍最优。

## 来源与致谢

实验使用 SDSC/NPACI 历史计算作业的 SWF 镜像，保留原始文件头和 job_id。
SDSC 作业日志由 Victor Hazlewood 提供；字段解释见 [SWF 规范](https://www.cs.huji.ac.il/labs/parallel/workload/swf.html)，原始日志及使用条件见 [Parallel Workloads Archive](https://www.cs.huji.ac.il/labs/parallel/workload/l_sdsc_sp2/)。

Copyright 2000 The Regents of the University of California. All Rights Reserved.

此处仅按教育用途进行模拟分析；源数据的权利声明与来源在派生材料中保留。仓库不提交完整源日志或大体量运行目录，只保存可复现参数、必要摘要、图和报告；本地完整证据保留于指定输出目录。
