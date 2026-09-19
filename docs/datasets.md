# 演示数据与负载复用

当前 `examples/demo-job.json` 是自建模拟打印任务，不含真实个人文档。

可选负载来源为 Parallel Workloads Archive 的 SDSC SP2 作业日志。它是历史计算作业日志，不是真实校园打印记录，不能据此声称“还原实际校园打印需求”。

- [官方说明](https://www.cs.huji.ac.il/labs/parallel/workload/l_sdsc_sp2/)
- [SWF 字段](https://www.cs.huji.ac.il/labs/parallel/workload/swf.html)
- [WRENCH 清洗版镜像](https://raw.githubusercontent.com/wrench-project/task_clustering_batch_datasets/master/workloads/sdsc_sp2.swf)

已提供 A 的 `tools/convert_swf.py` 与 `scheduler-replay`，命令见 [回放说明](replay.md)。建议先处理一个连续窗口的 500–2,000 条有效记录。转换器检查 18 列和必需字段、处理缺失值，保留 SHA-256、转换时间及下载时间（仅工具直接下载时可知）。

建议映射：提交时间 → 模拟到达时间；运行时长 → 人为换算打印页数；用户编号 → 匿名提交者。处理器数量不直接等于打印机数量，应丢弃或作为单独研究变量并说明含义。

转换必须保存页数换算比例、时间缩放、筛选条件、固定随机种子（若用）、原始 job_id。历史 wait time 是原调度器的结果，不能直接作为新算法的等待时间；重新计算新调度结果。

保留原始文件的版权声明和来源引用。原始日志已通过 `*.swf` 忽略，不把大文件或用户真实文档默认提交到 Git。
