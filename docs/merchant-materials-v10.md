# v10 连续摄影纸面

工具：Codex 内置 `image_gen.imagegen`，使用已有批准概念图进行精确编辑。仅生成无文字背景板，按钮、文字、指标、图表仍为 Qt 绘制与真实交互。新文件与原始概念资源并存，没有覆盖原图。

## 业务页素材

参考：`assets/merchant/workbench.png`。
交付：`assets/merchant/hero-paper-v10.png`，1586×992。
原始生成文件：`exec-735059d5-501b-44e8-a081-d3bf8792f0a8.png`。
Qt 使用原图区域 x=0,y=56,width=1134,height=236，对应四页共同顶部区域；不再把两块照片贴到平铺纸纹上。

提示词要求：Edit this exact 1586x992 reference in place, keeping the original canvas dimensions. Erase all typography, logos, navigation, tables, document previews, controls, statistics, separators and borders. Preserve only the continuous ivory paper and original leaves, coffee and complete gold binder clip at the original scale and positions. Recreate continuous photographic daylight and soft shadows across x0..1134,y56..292, without pasted rectangles, abrupt tonal seams or tiled patches. Keep x940..1134,y206..280 clear for live revenue text. Do not enlarge or relocate the decorations. Output a fully text-free photographic background plate.

## 调度页素材

参考：已批准的调度工作台预览，保存为 `docs/images/dispatch-approved.png`。
交付：`assets/merchant/dispatch-paper-v10.png`，1586×992。
原始生成文件：`exec-41132b22-bd14-4875-a863-7c4c6887f935.png`。
Qt 使用原图区域 x=0,y=56,width=1586,height=162，对应预览中的整幅纸面横幅。

提示词要求：Edit the approved 1586x992 UI reference in place, preserving exact dimensions and composition. Erase all typography, logos, navigation, controls, tables, cards, colored timelines, borders, rules and footer. Preserve only the continuous natural off-white paper in x0..1586,y56..218, green leaves at x0..90,y56..198, and the complete gold binder clip at x1410..1545,y80..213 with its natural soft shadow. Keep original positions and scale, no coffee cup, no pasted rectangles or seams. Everything outside the banner is nearly white blank paper. Deliver the full image as a background plate, not a redesigned UI.
