# 课堂扫码演示

手机浏览器 → HTTP 网页入口 → 现有 coordinator 本地 IPC → 同一调度器与执行进程。
老板 Qt 窗口与网页读取同一个状态目录，手机下单后老板窗口自动更新。文件真实上传并保存在电脑，打印与支付仍是课程项目的模拟实现，不连接实体打印机。

## Windows 本地启动

先确保 `build/ci/bin` 或 `build/dev/bin` 中已有可运行的老板端与后台（构建方法见 README；脚本优先 Release），安装二维码依赖：

```powershell
python -m pip install --target runtime/classroom-python -r tools/classroom/requirements.txt
```

在项目根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\start-classroom-demo.ps1
```

脚本打开老板窗口和浏览器的二维码展示页，使用 `runtime/classroom-demo`，不会自动塞入预设订单。
它默认监听 `0.0.0.0:8787`，按电脑默认路由选出局域网地址。保持终端打开；Ctrl+C 停止网页入口，老板窗口和后台独立运行。
手机和电脑连同一 Wi-Fi，或让手机连电脑热点，再扫码即可。先用一部手机验证能打开并提交，再向全班展示。
Windows 若询问防火墙访问权限，允许当前演示网络上的访问。若手机打不开，检查地址、8787 入站规则以及 Wi-Fi 是否隔离客户端；可以改用电脑热点。

多网卡、VPN 或热点地址选错时手动指定（替换成电脑实际 IPv4 地址）：

```powershell
powershell -ExecutionPolicy Bypass -File .\start-classroom-demo.ps1 -PublicUrl http://192.168.137.1:8787
```

仅电脑浏览器自测：

```powershell
powershell -ExecutionPolicy Bypass -File .\start-classroom-demo.ps1 -BindAddress 127.0.0.1 -PublicUrl http://127.0.0.1:8787
```

二维码页为 `/present`；终端打印含课堂码的扫码入口和课堂码本身。没有浏览器身份时打开 `/` 显示输入课堂码的页面（不区分大小写），扫码则直接进入。

学生端只保留网页，原 Qt/Windows 学生界面已移除；按手机竖屏优先设计，宽屏（≥1024 px）切换为左侧纸面、右侧设置的桌面布局，横屏手机为左右两栏：

- **下单打印**：选择文件后在纸面预览，左右滑动或 ‹ › 换张，双面可翻面，可旋转、放大阅读；黑白 / 彩色、A4 / A3、单双面、页数与份数、三档打印服务。可一次选择多个文件（最多 10 个），逐个上传后合并为一张订单：合计页数为各文件之和（自动识别的 PDF/图片页数锁定不可改，未识别的沿用估计页数），订单标题显示为“首个文件名 等N份”，纸面与放大阅读器按文件顺序连续翻页；预览与阅读器以首个文件呈现，列表可单独移除任一文件后重新合计。三档价格、预计取件与承诺期限都来自后台 `quote`，选文件前也按示例文档报价。底部固定栏显示总价与“确认并下单”，再从底部弹出模拟支付面板（微信 / 支付宝 / 校园卡）。学号 / 昵称选填，作为老板端看到的称呼。多文件合并请求带 `documents:[{upload_id, pages}]`，单文件仍用原 `upload_id` 形状，coordinator 与老板窗口协议不变。
- **我的订单**：本浏览器的订单、状态、排队顺位与预计时间、打印进度、取件码，等待或打印中可取消并模拟退款。
- **排队情况**：当前策略、四项全店指标、六台设备状态与进度、按当前策略推算的等待队列。其他同学的文档名和称呼不会发到手机，只显示“其他订单”与规格。

页数识别：PDF 由网关用 PDFium（`python tools/setup_pdfium.py` 安装到 `.tools/pdfium`）读取页数并渲染预览，页数随文档锁定；Word / PPT / Excel 在展示电脑装有 LibreOffice 时于后台逐个转换为 PDF 后同样处理（依赖发现顺序见 [Office 预览](office-preview.md)），未安装时提示学生手动填写页数；图片固定 1 页；文本按约 50 行一页估算，可修改。图片与文本由网关的 `GET /api/document/<upload_id>` 端点按会话归属持久提供预览（仅放行 image / text 类型，PDF/Office 走渲染页端点），刷新或重进页面后仍能预览，不再依赖浏览器本地 blob。

二维码页同时显示课堂码、全店数量、设备进度和匿名等待队列，适合投屏讲解策略对队列顺序的影响；Qt 老板窗口用于讲解具体订单调度。

## 课堂演示顺序

1. 老板后台先暂停派发，投屏二维码页，让同学上传并下单。PDF 自动识别页数；Office 在未安装 LibreOffice 时手动填写实际页数。
2. 回到老板工作台，观察队列。选择 FCFS 后恢复派发，展示先到先处理。
3. 再暂停，收集一批长短不同的任务，选 SJF 后恢复，观察短任务优先。已有运行任务继续执行，不抢占。
4. 让同学选择 A3 彩色双面，观察兼容设备；暂停 A3 设备接单，再恢复，网页预计时间同步变化。
5. 同学在手机上看自己订单进度，老板后台同时显示同一任务。完成后查看经营统计。

默认基准 `1000 ms/打印面`，异构设备有倍率，便于看清进度。修改 `-PageMs` 只对新启动的后台有效；已有后台沿用旧参数。
重新开一场演示可以指定新目录，例如 `-StateDirectory runtime/classroom-next`，旧目录及订单会保留。
切换策略的现场演示用来说明行为；严格的同负载性能比较仍使用已有 `shop-experiments`。

## 手动运行 / macOS / Linux

二维码依赖安装到自己的 Python 环境后，启动老板窗口与网页服务：

```sh
./build/dev/bin/campus-print --data-dir runtime/classroom-demo --demo-mode --page-ms 1000
python tools/classroom_web.py --coordinator build/dev/bin/print-coordinator --state-dir runtime/classroom-demo --host 0.0.0.0 --public-url http://YOUR-LAN-IP:8787 --open-browser
```

Windows 可执行文件加 `.exe`，并让 Python 子进程的 PATH 包含匹配的 Qt 运行库目录。
服务要求已有经营模式后台，启动前会检查；不会改写 `state.json` 或自动创建另一套调度逻辑。

## 之后接入域名和 ECS

让调度和老板窗口继续在演示电脑运行，ECS 只做 HTTPS 反向代理。电脑发起 SSH 反向端口转发，因此不需要教室网络提供公网入站地址。
公网演示启动网页服务时用 `--host 127.0.0.1 --public-url https://print.example.com`；Windows 脚本对应：

```powershell
powershell -ExecutionPolicy Bypass -File .\start-classroom-demo.ps1 -BindAddress 127.0.0.1 -PublicUrl https://print.example.com
```

在演示电脑另开终端（替换用户与 IP，非 22 端口加 `-p PORT`）：

```sh
ssh -N -T -o ExitOnForwardFailure=yes -o ServerAliveInterval=15 -o ServerAliveCountMax=3 -R 127.0.0.1:18787:127.0.0.1:8787 user@ECS_IP
```

ECS 的 SSH 服务需要允许 TCP 转发。远端端口绑定 ECS 的回环地址，无需在安全组开放 18787，也无需开启全网 `GatewayPorts`。
域名 DNS 指向 ECS，ECS 配置域名证书并开放 HTTPS，再按 [nginx.conf.example](../tools/classroom/nginx.conf.example) 替换域名与证书路径，检查配置后加载。
如果已有 nginx 站点，合并这一个子域名的 server 配置，保留原有站点；不要用示例覆盖整个 nginx.conf。
手机流量也能访问这个 HTTPS 域名。演示电脑、网页进程和 SSH 隧道都需在线；隧道断开时入口不可用，订单仍由本地后台执行。

以上依据 [OpenSSH 的反向端口转发说明](https://man.openbsd.org/ssh) 与 [nginx 代理模块文档](https://nginx.org/en/docs/http/ngx_http_proxy_module.html)。`oscd.we1chan.com` 的实际 ECS 检查结果和对应配置见 [这台 ECS 的部署记录](ecs-oscd.md)；当前已安装并验证 Nginx 与 Certbot，域名配置已保存备用，网站端口尚未放行。

### 阿里云 ECS 配置步骤

以下使用 `print.example.com`、`ECS_IP`、`user` 作为占位符，操作时替换为实际子域名、公网 IPv4 和 SSH 用户。
ECS 只需运行 nginx 与 SSH 服务，不必在服务器安装 Qt、上传整个 C++ 项目或复制打印数据目录。

1. **确认地域与备案。** 在 ECS 实例详情查看地域和操作系统。如果在中国内地，通过域名对外提供 Web 服务需要完成 ICP 备案；域名已有其他接入商的备案时，核对是否需要接入阿里云。依据 [阿里云备案场景说明](https://help.aliyun.com/zh/icp-filing/what-scenarios-need-to-be-filed-in-aliyun) 与 [域名解析建站准备](https://help.aliyun.com/zh/dns/pubz-add-website-parsing)。中国香港等非中国内地实例的 ICP 要求不同，按地域核对。

2. **配置安全组。** ECS 控制台 → 实例 → 目标实例 → 安全组，进入所绑定安全组，添加入方向允许规则。TCP 80、443 的授权对象为 `0.0.0.0/0`，用于公网 HTTP/HTTPS；TCP 22（若 SSH 改了端口则用实际端口）允许演示电脑的公网出口 IP，例如 `YOUR_PUBLIC_IP/32`。`18787` 和 `8787` 无需向公网开放。实例系统防火墙若已启用，也要允许网站端口；保留原有管理连接规则。参见 [阿里云安全组说明](https://www.alibabacloud.com/help/zh/ecs/user-guide/start-using-security-groups)。

3. **配置域名解析。** 在域名当前的 DNS 服务商添加记录。如果使用阿里云云解析 DNS，进入公网权威解析 → 域名 → 解析设置 → 添加记录：类型 `A`，主机记录 `print`，记录值 `ECS_IP`，线路“默认”。这会得到 `print.example.com`，不要填电脑的 `10.x.x.x` / `192.168.x.x` 地址。若域名的 NS 不在阿里云，要到实际 DNS 服务商配置。参见 [阿里云 A 记录说明](https://help.aliyun.com/zh/dns/pubz-add-parsing-record/)。

4. **安装 / 使用现有 nginx，配置 HTTPS。** 已有宝塔或 nginx 时使用现有安装，只添加这个子域名的站点配置。新 Ubuntu / Debian 服务器可在 SSH 终端执行：

   ```sh
   sudo apt update
   sudo apt install nginx
   sudo systemctl enable --now nginx
   ```

   其他系统先按对应系统的包管理器安装；不要在未知系统直接套用 apt 命令。安装参考 [nginx 官方包说明](https://nginx.org/en/linux_packages.html)。

   准备匹配 `print.example.com` 的 HTTPS 证书。已有阿里云数字证书时，在证书控制台下载 **Nginx** 格式，将证书 `.pem` 与对应私钥 `.key` 文件放在 ECS 上自己的证书目录；参见 [阿里云证书下载说明](https://help.aliyun.com/zh/ssl-certificate/download-an-ssl-certificate)。私钥由自己保管，不需要发到聊天中。

   将 `tools/classroom/nginx.conf.example` 复制为 nginx 的一个站点配置，例如 `/etc/nginx/conf.d/classroom-print.conf`，替换两处 `server_name` 和两条证书路径。示例路径 `/etc/letsencrypt/live/...` 只是占位；阿里云下载的文件应填写它们在 ECS 上的真实路径。
   nginx.conf 必须在 `http {}` 中 include 这个配置。示例中的 `limit_req_zone` / `limit_conn_zone` 放在 `http` 上下文，不能粘贴到宝塔某个站点的 `server {}` 内。宝塔场景先用面板配置站点 HTTPS 与指向 `http://127.0.0.1:18787` 的反向代理，再按对应的主配置位置设置限流与 `client_max_body_size 10m`。

   检查通过后再加载配置：

   ```sh
   sudo nginx -t
   sudo systemctl reload nginx
   ```

5. **在 Windows 演示电脑启动。** 先停止同端口的旧网页服务（老板窗口与队列可以保留），在项目根目录开一个 PowerShell 终端：

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\start-classroom-demo.ps1 -BindAddress 127.0.0.1 -PublicUrl https://print.example.com
   ```

   终端保持运行，网页服务只监听本机 8787。自动打开的公网页面在建立下一步的隧道之前可能暂时不可访问。

6. **在同一电脑第二个终端建立隧道。**

   ```powershell
   ssh -N -T -o ExitOnForwardFailure=yes -o ServerAliveInterval=15 -o ServerAliveCountMax=3 -R 127.0.0.1:18787:127.0.0.1:8787 user@ECS_IP
   ```

   首次连接按 SSH 提示核对服务器指纹并登录；用户为实际已配置的 SSH 用户。非 22 端口添加 `-p PORT`，密钥登录添加 `-i YOUR_KEY_PATH`。正常情况下连接后没有输出，并持续运行。`-R` 将 **ECS 本机的 18787** 连接到 **演示电脑本机的 8787**；这条命令必须在演示电脑执行，不能在 ECS 上执行。

7. **分层验证。** 在 ECS 的另一个 SSH / Workbench 终端执行：

   ```sh
   curl --fail http://127.0.0.1:18787/health
   ```

   返回 `{"ok": true}` 表示隧道能访问本机调度后台。然后在电脑浏览器打开 `https://print.example.com/health`，应得到相同结果；最后打开第一终端打印的完整 `https://print.example.com/join?code=...` 链接，再访问 `/present` 展示二维码。
   用手机流量试扫码、上传与下单，确认老板窗口出现同一订单后再开始课堂互动。直接访问 `/` 时出现课堂码输入页表示浏览器尚无身份，扫码或输入课堂码即可进入。

代理与隧道的配置依据 [nginx proxy_pass](https://nginx.org/en/docs/http/ngx_http_proxy_module.html#proxy_pass) 和 [OpenSSH -R](https://man.openbsd.org/ssh)。示例限流为每个来源 IP 每秒 100 请求，并保留突发额度，以容纳同一校园 Wi-Fi 出口的多人状态轮询；这是演示参数，不代表已完成整班或公网压力测试。

常见问题：

- `remote port forwarding failed`：ECS 的 18787 被其他进程或旧隧道占用，或 SSH 服务不允许远程端口转发。核对现有连接、sshd 的 `AllowTcpForwarding` / `PermitListen`，不要直接停止无关进程。
- 域名显示 `502`：先查 ECS 的 `127.0.0.1:18787/health`，再查电脑的 `127.0.0.1:8787/health`。两端终端都需保持运行；检查 nginx 上游地址与 SSH 转发端口一致。
- 手机连接超时：核对 DNS 返回公网 IP，安全组与系统防火墙开放 443，nginx 正在监听。
- 证书警告：核对证书覆盖所用子域名、证书链及有效期；修复配置后再访问。
- 无法从 nginx 连接到上游且系统启用 SELinux：查看 nginx 与系统审计日志，按系统策略允许 nginx 访问这个上游；不需要关闭 SELinux。
- 上传返回 `413`：检查文件不超过 10 MiB，nginx / 面板的请求大小设置允许 10 MiB。

以上是配置步骤与模板，未代替在你的 ECS 上执行和验收。

## 数据与演示范围

- 每次数据目录保存自己的课堂码与签名密钥；每个浏览器的签名 Cookie 对应独立身份。Cookie 保留时刷新 / 重启网页服务后可找回订单；换浏览器或清除 Cookie 产生新身份。
- 网页只提供上传、报价、提交、查看与取消自己的订单，不向网络暴露策略修改、设备操作、退款或完整 IPC 快照。
- 文件以随机 ID 保存为 `<state-dir>/classroom-web/uploads/*.bin`，另有文件名、大小、SHA-256、页数元数据；Office 转换结果为同名 `.pdf`，预览页缓存在 `<id>.preview/`。只有上传者的浏览器能读取自己文件的预览页，没有原文件下载接口。不要上传敏感文件。
- 上传上限 10 MiB/文件、10 文件/浏览器、300 文件/演示目录、总共 300 MiB。PDF 损坏或加密时拒绝上传；Office 转换超时 90 秒，每次使用独立临时副本与配置（宏安全级别 3），同一时间只转换一个，最多排队 30 个，失败或排满时改为手动填写页数。网关重启时未完成的转换改为手动填写。
- 一个上传对应一个稳定订单 ID：上传 ID 的 UUID 形式，与 Qt 客户端相同，取件码因此为 6 个随机字符。旧版网关的 `web-<上传 ID>` 订单仍归原浏览器所有。重复点击 / 回复丢失后再次提交相同规格不会生成第二张订单；更改已提交规格须重新选择文件。后台超时不自动重发，先核对“我的订单”。
- 订单归属只由签名 Cookie 决定；`Job::customer` 只保存学生填写的学号 / 昵称（≤32 字符，未填写为“访客 XXXX”），供老板端显示，填写别人的学号也看不到别人的订单。
- 收款码默认不显示，支付面板显示“收款码待配置”。只有展示者把图片放到 `<state-dir>/payment-codes/wechat.png|jpg`、`alipay.png|jpg` 时才向手机展示；网页不会自动启用仓库中的收款素材，避免同学误扫码产生真实转账。应用内“确认支付”始终是模拟。
- 网页每 1.5 秒读取一次状态，服务共用约 1 秒的快照缓存。预计时间由原调度器计算，不含后续到达、重试或故障。
- 这是短时课堂互动服务。公网入口使用给出的 nginx HTTPS、限流和请求大小配置。展示页没有管理权限，但课堂码持有者可以加入演示，应只在课堂分享。
- 结束后保留 / 删除本次目录中的文件由展示者决定；停止网页不会自动删除上传文件。

## 验证

```sh
python tests/classroom_web_test.py --coordinator build/dev/bin/print-coordinator --office-stub build/dev/bin/office_converter_stub
ctest --preset dev -R '^classroom_web$'
```

测试启动独立临时目录的真实 coordinator，覆盖上传、三档后台报价、选文件前报价、模拟打印输出、并发下单、浏览器隔离、匿名队列、显示名与归属分离、取消权限、重复提交、重启后身份恢复、输入大小限制、课堂码页面、收款码配置，以及 PDF 页数锁定与预览渲染（需 `.tools/pdfium`，缺失时跳过）和经转换器替身的 Office 转换与失败回退。
页面布局在本机用无头 Chrome 以 390×844 竖屏、844×390 横屏和 1440×900 桌面尺寸人工检查过；真机扫码与整班并发仍需在演示网络中实测。
安装 qrcode 时另验证二维码 SVG；未安装时只跳过这个可选依赖测试。实际启动扫码演示应安装该依赖。
