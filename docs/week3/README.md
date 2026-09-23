# 第三周小组设计汇报

项目：校园共享打印任务调度与故障恢复系统。16 页中文 PPT，预计 8–10 分钟，包含约 1 分钟交互原型演示。

## 文件怎么用

- `第三周小组设计汇报.pptx`：课堂汇报，文字、UML 和思维导图均为可编辑对象，备注中有讲解和来源。
- `第三周小组设计汇报.pdf`：由最终幻灯片逐页渲染成图片的投影备用文件，便于 Windows/macOS 之间传递；修改文字请使用 PPTX。
- `校园共享打印-设计图稿.drawio`：在 [diagrams.net](https://app.diagrams.net/) 选择“打开现有绘图 / 设备”导入。内含思维导图、用例图、架构图、核心类图、时序图、状态图和原型线框共 7 页。
- `diagrams/`：每张图的独立 SVG 和 PNG，浏览器可打开，适合插入报告。图稿可以在 `.drawio` 文件中继续修改。
- `prototype.html`：双击后用浏览器打开，不需要服务器、Qt 或账号，Windows/macOS 均可使用。
- `讲稿.md`：逐页讲解提纲，建议 A 讲需求和调度、B 讲进程与恢复、C 讲原型和集成计划。
- `source/`：PPT 与图稿生成源文件，普通使用者无需运行。

PPT 使用本机可用的 Hiragino Sans GB（冬青黑体）。Windows 未安装该字体时，可以在 PowerPoint 中替换为微软雅黑并检查换行；直接使用 PDF 投影可保持本次版式。不要把原型截图用作正式系统的运行验收证据。

## 第三周要求对应

依据用户提供的《操作系统原理课程设计.pdf》第 13–14 页。原 PDF 未复制到仓库。

| 课程要求 | 本材料位置 |
| --- | --- |
| 选题、要做的事情与需求目标 | PPT 1–3 页 |
| 思维导图 | PPT 4 页，`diagrams/mindmap.svg` |
| UML 工具与概要设计 | PPT 5–9 页，`.drawio` 中的用例、类、时序、状态图 |
| OS 原理应用，必须有守护进程 | PPT 6、10 页，区分 POSIX daemon 与 Windows 前台监督 |
| 原型工具及界面设计 | PPT 11–13 页，线框图及 `prototype.html` |
| 可能难点 | PPT 14 页 |
| 小组分工 | PPT 15 页 |
| 后续进度安排 | PPT 16 页 |

课件要求第三周提交到云邮平台，本次生成的是可自行提交的本地材料，尚未上传课程平台。课件第 13 页写每组 4–6 人；本材料按用户已确认的三人安排制作，提交前请向老师确认。成员目前使用 GitHub 名称，正式提交时可换成真实姓名与学号。

## 原型工具是什么

原型工具用来提前画出页面、按钮与操作流程，让团队在完整实现前讨论“用户怎么用”。常见工具包括 Figma、Axure 和墨刀。它们一般不承担真正的调度、进程管理或持久化。

本次提供两种可直接使用的形式：

1. **可编辑线框和流程**：diagrams.net 格式，适合展示页面组成及跳转关系。draw.io 官方说明其可用于 [界面线框和 mockup](https://www.drawio.com/docs/tutorials/mockups-in-jira/)。
2. **离线交互原型**：用 HTML/CSS/JavaScript 表达界面行为，直接在浏览器中演示。它展示的目标与 [Figma 的交互原型](https://help.figma.com/hc/en-us/articles/360040314193-Guide-to-prototyping-in-Figma)类似，但不是 Figma 工程文件，也没有连接正式 C++ 后台。

这次没有生成 Axure `.rp` 或 Figma `.fig` 文件。课程 PDF 没有指定原型软件；如果教师另有指定，应使用对应软件重新整理原型。

## 原型演示顺序

1. 点击“恢复示例任务”，恢复课堂初始状态。
2. 查看两台打印机与等待队列，切换策略，说明它只影响下一次派发。
3. “提交任务”中填写名称、页数与份数，提交后在队列中查看。
4. 取消等待任务，再取消一个运行中的任务，展示确认框和“取消中”。点击“推进 5 秒”模拟收到停止确认。
5. 点击“断开连接”，再“推进 5 秒”，最后“重新连接”，展示获取最新快照的交互。
6. 可选：点击“模拟设备故障”，在“运行记录”中查看演示重试事件。

原型数据仅保存在当前浏览器的本地存储中，键为 `cps-week3-prototype-v1`。示例使用 0.5 模拟秒/页便于展示交互，正式算法实验采用的参数见 `experiments/config.json`。原型不启动进程、不访问打印机，也不是故障恢复正确性的测试替代品。

## 内容依据与当前边界

设计依据：课程 PDF 第 3、7–14 页，仓库 `docs/team.md`、`docs/interfaces.md`、`docs/architecture.md` 以及对应 C++ 实现。内容核对基线为 `fb7de0b`。

需求是基于校园共享打印场景提出的假设，没有虚构问卷、访谈人数或校园真实打印数据。图示目标架构采用独立 coordinator；后台实现已存在，GUI 当前仍使用内存调度器并直接启动 supervisor，接入持久后台仍待团队联调。图上的目标关系和现有实现边界均在 PPT/备注中说明。

## 源文件重建

`source/build_deck.mjs` 使用 JavaScript ES modules 和 `@oai/artifact-tool`，`source/diagrams.mjs` 是 PPT、SVG 和 draw.io 图稿共用的定义。需要提供可用的 Node.js 与该依赖，设置 `CPS_PRESENTATION_SKILL`、`CPS_RUNTIME_PYTHON`、`RUNTIME_NODE_MODULES`，并让源文件目录能解析包。截图来自同目录交互原型，构建时从项目根目录的 `runtime/week3-build/prototype-queue.png` 和 `prototype-cancel.png` 读取。

`source/capture_prototype.mjs` 使用 Playwright 与本机 Chrome，自动检查原型交互并生成截图。在项目根目录运行 `node docs/week3/source/capture_prototype.mjs`；Chrome 未安装在默认位置时可通过 `CPS_CHROME_PATH` 指定可执行文件。浏览器打开 `prototype.html` 本身不需要这些依赖。

验证器不覆盖已存在的 PPT，可用 `CPS_FINAL_PPTX` 指定项目内一个新文件名。该构建方式面向材料维护者，不属于项目 C++ 构建或 CI 的依赖。
