# 学生网页端交接与部署

当前学生端只提供网页。旧 Qt/Windows 学生窗口、纸叠动画、本地预览控件和 VBS 启动入口已移除；`campus-print` 与 `campus-print-merchant` 只提供老板后台。

## 接手与模块

开始工作先核对 `git status --short --branch`、当前分支和远端；保留已有修改。网页 HTTP 和部署约定见 [classroom-demo.md](classroom-demo.md)，coordinator 契约见 [interfaces.md](interfaces.md)。

| 路径 | 责任 |
| --- | --- |
| `tools/classroom/index.html`、`app.js`、`style.css` | 上传、预览、打印规格、报价、模拟支付、我的订单与匿名队列 |
| `tools/classroom/join.html` | 课堂码与签名 Cookie 会话入口 |
| `tools/classroom/present.html`、`present.js` | 投影二维码与队列展示 |
| `tools/classroom_web.py` | HTTP 网关、上传存储、PDF 渲染、异步 Office 转换与受限 IPC 请求 |
| `start-classroom-demo.ps1` | Windows 启动老板窗口和网页网关，打开二维码页 |
| `assets/editorial/brand-64.png`、`brand-128.png` | 网页品牌与 favicon |
| `tools/setup_pdfium.py`、`tools/setup_office_preview.py` | 网页预览依赖的本地安装 |
| `tests/classroom_web_test.py`、`tests/office_web_preview.py` | HTTP/后台集成及六格式真实 Office 预览验收 |
| `tools/package_app.py`、`tests/package_smoke.py` | 老板端及网页资源打包、独立运行检查 |

`src/gui/editorial_chrome.*` 是老板端仍在使用的窗口与对话框控件，不是学生客户端。`PrintController`、`PrintWindow`、`paper_workspace.hpp` 继续支撑老板端和课程调度演示。

## 行为与协议边界

- 学生使用电脑或手机浏览器访问网关。上传原件保存在指定数据目录的 `classroom-web/uploads/`，不进入 `state.json` 或 coordinator IPC；后台只接收订单元数据。
- 网页下单接受后，在同一数据目录 `order-previews/` 为老板端保存 PDF / Office 首页 PNG、原始 PNG / JPEG 或有上限的文本首页摘要；多文件订单采用第一个文件，预览失败不撤销订单。封面文件没有新增 HTTP 公开路由，也不改变 coordinator IPC。老板端可为历史订单另选本机封面图片。
- 上传限制 10 MiB、总存储限制 300 MiB；转换后的 PDF 上限 64 MiB、页数上限 1000。具体支持格式和失败行为以 HTTP 实现与课堂说明为准。
- 订单归属由签名 Cookie 和上传记录判断，学生填写的学号/名称只作显示。报价、预计时间与订单状态来自共同后台。
- 网络只开放学生操作，不接受任意 IPC 转发。保留课堂码、CSRF、上传归属与重复下单校验。
- 付款确认和打印仍为模拟；展示收款码不表示自动确认真实到账。
- 收款码由 `<state-dir>/payment-codes/wechat.png` / `.jpg` / `.jpeg` 和 `alipay` 同名文件配置。公开素材仍在 `assets/editorial/payment-codes/`，网页不会自动使用这些素材作为默认收款人。

## 构建、启动与检查

```powershell
cmake --preset ci -DCMAKE_PREFIX_PATH="<Qt目录>"
cmake --build --preset ci --parallel 2
python tools/setup_pdfium.py
python -m pip install --target runtime/classroom-python -r tools/classroom/requirements.txt
./start-classroom-demo.ps1
```

Office 转换按 [office-preview.md](office-preview.md) 安装。Qt 构建不再依赖学生端 C++ PDF/Office 预览；Python PDFium 供网页网关使用。

启动脚本优先 `build/ci/bin`，其次 `build/dev/bin`，发布目录使用同级 `campus-print.exe`。老板端 `--data-dir` 与网页 `--state-dir` 必须为同一绝对路径；默认课堂目录为 `runtime/classroom-demo`。`--role student` 已不支持并返回 64。

验收顺序：构建 → CTest → 网页上传/报价/下单与老板端共享队列 → 如需发布则打包并验包 → 更新启动入口。独立发布要求已提交且 clean 的工作树、匹配 Qt、完整文件清单与许可证；发布流程见 [release.md](release.md)。

关闭网页网关和老板窗口后，coordinator 仍可能运行。清理前按 [clean-up.md](clean-up.md) 停止对应数据目录的后台；旧发布包、用户文件和运行数据不随源码移除而删除。

旧桌面设计和测量仅作历史记录，保存在 [editorial-frontend.md](editorial-frontend.md)，不作为当前网页的启动或验收说明。
