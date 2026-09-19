# 调度统计与负载回放

A 的命令行工具，可在接入 B 的后台服务和 C 的界面前独立使用。纯 C++ `cps::replay` 运行事件模拟；`scheduler-replay` 仅用 Qt Core 读取和写入 JSON，不启动 GUI、工作进程或真实打印机。

## 自建负载

按 `docs/development.md` 配置 Qt 并构建 `dev`；Windows 使用 Developer PowerShell 并先让 Qt 的 `bin` 进入 PATH。macOS/Linux：

```sh
cmake --build --preset dev
./build/dev/bin/scheduler-replay --input examples/workload.json --output comparison.json
```

Windows PowerShell：

```powershell
cmake --build --preset dev
.\build\dev\bin\scheduler-replay.exe --input examples/workload.json --output comparison.json
```

默认同时运行 FCFS、SJF、PriorityAging，2 台同速打印机，150 ms/页，30,000 ms 老化一级。可用 `--policy fcfs|sjf|priority|all`、`--printers 1..64`、`--page-ms`、`--aging-ms` 改变参数。输出目录须先存在；报告原子写入，不能覆盖输入文件。建议把运行结果放入已忽略的 `runtime/`。

回放假设没有失败、取消、进程启动开销、机器速度差异或换纸时间，适合比较算法本身；不能把回放时间当真实设备实测。状态接口支持的恢复功能不在此基线模型中注入故障。

## SDSC SWF 转换

需要 Python 3.10+，不需要第三方 Python 包。以下命令在 macOS 用 `python3`，Windows 可用 `python` 或 `py -3`。

```sh
python3 tools/convert_swf.py --output runtime/sdsc-1000.json --count 1000
```

默认下载文档中的 WRENCH 镜像；已有本地 SWF 时：

```sh
python3 tools/convert_swf.py --input runtime/data/sdsc_sp2.swf --source-url https://raw.githubusercontent.com/wrench-project/task_clustering_batch_datasets/master/workloads/sdsc_sp2.swf --output runtime/sdsc-1000.json --count 1000 --time-scale 0.01 --seconds-per-page 60
./build/dev/bin/scheduler-replay --input runtime/sdsc-1000.json --output runtime/sdsc-comparison.json
```

- 每行必须有 18 个有限数值，`-1` 缺失值仅在未使用字段允许；选择 status=1、非负提交时间、正运行时长，跳过重复 job_id，按提交时间稳定排序。
- `--offset` 是过滤和排序后的记录偏移；`--count` 为 1–10,000，数据不足时报错，不静默缩小样本。
- 到达时间 =（原提交秒数 − 窗口首条提交秒数）× 1000 × time-scale，毫秒四舍五入。
- 页数 = ceil（原运行秒数 / seconds-per-page），截到 1..max-pages；默认上限 1,000，并记录截断数量。
- 份数固定为 1。历史等待时间和处理器数不参与新调度；后者不等于打印机数量。
- 默认 priority 全为 0。在无重试基线中，老化优先级会和 FCFS 产生相同顺序，这是正常结果。要观察不同优先级，可显式加 `--priority-mode job-id-modulo --priority-levels 5`：用原 job_id 对 5 取模，只是合成示例，与真实身份或紧急程度无关。
- 所有转换参数、源文件 SHA-256、原始文件头、原 job_id/行号、用户编号、筛选计数都保留。下载时记录 retrieved_at_utc，本地转换时该字段为空；converted_at_utc 是转换时间，不能冒充下载时间。没有随机抽样。

这是计算作业到打印任务的人工映射，不代表校园真实需求。来源和版权见 `docs/datasets.md`。源日志和运行结果默认不提交。

## 输入与输出

输入为 `schema_version: 1`、`kind: campus-print-workload`、可选 source 对象、jobs 数组。每条任务需要 job_id、title、pages、copies、submitted_ms；priority 可省略（默认 0）。输入最多 64 MiB / 10,000 条；未知任务字段保留在原始负载中，调度器不依赖它们。空数组可以回放，统计全部为 0。

报告为 `kind: campus-print-replay`，包含输入负载 SHA-256、来源元数据、回放参数和三种策略的 results。每种结果含 metrics、逐任务时间和 dispatches（printer 从 0 编号）。同一输入文件、参数和程序版本应得到相同输出；模拟不读取系统时间。

时间从最早任务到达起，推进到下一次到达或完成事件。同一时刻先处理全部完成，再提交全部到达任务，然后按打印机编号依次派发；不会提前提交未来任务。打印耗时 = 总页数 × page-ms。相同时间的任务保留输入顺序。

统计口径见 `docs/interfaces.md`。比较算法时固定输入、打印机数、速度与老化间隔，报告中同时查看平均/最长等待、周转和利用率。当前只交付运行工具与样例；新增自动化用例、正式实验报告和答辩材料按后续安排补充。
