# oscd.we1chan.com 的 ECS 部署记录

## 2026-10-02 部署前检查结果

通过登录后的阿里云内置浏览器查看实例详情、安全组，并在云助手对唯一目标实例执行了一次只读环境检查。

| 项目 | 实际结果 |
| --- | --- |
| 实例 | `i-2zea5yef4b7ten41m8hg`，名称 `we1chan`，运行中 |
| 地域、配置 | 华北 2（北京）H，2 vCPU、2 GiB |
| 系统 | Ubuntu 24.04.5 LTS |
| 公网 IPv4 | `39.105.20.207` |
| 安全组 | `sg-2zegsphaww0qttbm58xd` |
| 入方向规则 | TCP 22、TCP 3389、ICMP，来源均为 `0.0.0.0/0`；没有 TCP 80/443 |
| Nginx | 命令和 Ubuntu 软件包均未发现，服务不活跃 |
| 宝塔 | 未发现标准目录 `/www/server/panel` |
| SSH、Python | SSH 服务活跃，已安装 OpenSSH Server 与 Python 3.12 |
| 相关监听端口 | 只发现 SSH 22，未发现 80、443、8787、18787、8888 |
| UFW | 未启用 |
| 根分区 | 40 GiB，约 35 GiB 可用 |
| DNS | 实例执行 `getent ahostsv4 oscd.we1chan.com` 没有返回 IPv4 地址 |
| 备案 | 用户说明正在办理；本次未查询备案系统 |

云助手检查执行 ID 为 `t-bj06yswqgggeh34`，退出码 0，耗时 1 秒。检查脚本只读取系统、软件包、服务、相关监听端口与域名解析，没有安装软件或修改网络规则。
检查结果截图保存在本地忽略目录 `runtime/ecs-inspection/environment-check.png`。
“未发现宝塔”仅依据标准安装目录，不代表排除了所有自定义安装路径。

## 当前服务器准备状态

随后通过云助手执行 `oscd-prepare-nginx`，执行 ID 为 `t-bj06ysy2rbu9vk0`，安装并启动了 Nginx 与 Certbot。
验收结果：Nginx `1.24.0`，Certbot `2.9.0`，Nginx 服务 `active`，服务器本机 `http://127.0.0.1/` 返回 200，现有 Nginx 配置的 `nginx -t` 通过。
安装日志显示 Nginx 已设置开机启动，Certbot 的续期定时器已注册。没有签发域名证书。

域名配置已经备存在 ECS 的 `/root/oscd-deploy/oscd-classroom.conf`，尚未加载。
服务器配置文件 SHA-256 为 `61964882f4ca8966862698a4c25f5be0627e99292ccb5ddbc80c9d151d5915df`，与本地模板转换 LF 并添加末尾空行后的内容一致。
本次验证的是安装后的 Nginx 默认配置；域名 HTTPS 配置还需在证书就绪后运行 `nginx -t` 验证。
安全组仍未放行 80/443，域名 DNS、HTTPS 证书与展示电脑的 SSH 隧道尚未配置，公网课堂入口还不能使用。
验收截图保存在 `runtime/ecs-inspection/nginx-prepared.png`。

## 部署方式

手机 → `https://oscd.we1chan.com` → ECS Nginx → ECS 本机 `127.0.0.1:18787` → 电脑发起的 SSH 反向隧道 → 电脑 `127.0.0.1:8787` → 当前 Qt 调度器。

ECS 不需要安装 Qt 或保存同学上传的文件。老板窗口、队列与上传文件继续在展示电脑上运行；电脑和 SSH 隧道须全程在线。
北京属于中国内地，域名公开提供网站服务前先完成备案。参见 [阿里云备案场景说明](https://help.aliyun.com/zh/icp-filing/what-scenarios-need-to-be-filed-in-aliyun)。

### 已完成的服务器软件准备

本次已在这台 ECS 的云助手中，以 Shell 命令、目标实例 `i-2zea5yef4b7ten41m8hg` 执行安装和验收。以下命令供日后重建时参考：

```sh
apt-get update
apt-get install -y nginx certbot
systemctl enable --now nginx
nginx -t
systemctl is-active nginx
```

云助手本次使用 `root`；普通 SSH 用户执行这些命令时需要 `sudo`。

### 备案完成后的公网入口

1. 在域名实际 DNS 服务商添加 A 记录：主机记录 `oscd`，记录值 `39.105.20.207`。用 `nslookup oscd.we1chan.com` 确认结果。
2. 在上述安全组添加入方向允许 TCP **80 和 443**，来源 `0.0.0.0/0`。不添加 `8787` 或 `18787` 公网规则。现有 SSH 22 规则已能支持连接；本次没有修改它或 3389 规则。
3. 取得覆盖 `oscd.we1chan.com` 的 HTTPS 证书。新安装的 Ubuntu Nginx 默认站点使用 `/var/www/html`，可先验证 HTTP 可达，再运行：

   ```sh
   certbot certonly --webroot -w /var/www/html -d oscd.we1chan.com
   ```

   按提示填写邮箱并自行确认服务条款。成功后确认以下两个文件存在：

   ```sh
   test -s /etc/letsencrypt/live/oscd.we1chan.com/fullchain.pem
   test -s /etc/letsencrypt/live/oscd.we1chan.com/privkey.pem
   ```

   若使用阿里云证书，下载 Nginx 格式并在下一步配置中填写实际证书与私钥路径。不要将私钥发到聊天或提交 Git。
4. 将服务器上已准备的配置（对应 [nginx-oscd.conf.example](../tools/classroom/nginx-oscd.conf.example)）安装为 `/etc/nginx/conf.d/oscd-classroom.conf`。它已经填写域名和反向隧道端口，保留了 Certbot HTTP 验证路径。只安装这一份配置，避免和通用示例重复声明限流区域。证书文件存在后检查并加载：

   ```sh
   install -m 644 /root/oscd-deploy/oscd-classroom.conf /etc/nginx/conf.d/oscd-classroom.conf
   nginx -t
   systemctl reload nginx
   ```

   使用 Certbot 时，为续期后的 Nginx 重载设置 deploy hook：

   ```sh
   install -d /etc/letsencrypt/renewal-hooks/deploy
   printf '#!/bin/sh\n/usr/sbin/nginx -t && /usr/bin/systemctl reload nginx\n' > /etc/letsencrypt/renewal-hooks/deploy/oscd-nginx
   chmod 755 /etc/letsencrypt/renewal-hooks/deploy/oscd-nginx
   certbot renew --dry-run
   ```

### 展示电脑的两个终端

在项目根目录第一个 PowerShell 中启动老板窗口与网页服务：

```powershell
powershell -ExecutionPolicy Bypass -File .\start-classroom-demo.ps1 -BindAddress 127.0.0.1 -PublicUrl https://oscd.we1chan.com
```

第二个 PowerShell 建立反向隧道。下面的 `root` 须是你实际能登录的 SSH 用户；密码在 SSH 提示里自行输入，或按实际配置加 `-i` 指定密钥：

```powershell
ssh -N -T -o ExitOnForwardFailure=yes -o ServerAliveInterval=15 -o ServerAliveCountMax=3 -R 127.0.0.1:18787:127.0.0.1:8787 root@39.105.20.207
```

首次连接核对服务器指纹。云助手登录正常不代表已经验证了 `root` 的公网 SSH 登录密码或密钥；本次没有提供、读取或创建 SSH 凭据。
不要在 ECS 上执行这条隧道命令：它需要从运行 Qt 演示程序的电脑发起。

### 验收

在 ECS 云助手或 SSH 中执行：

```sh
curl --fail http://127.0.0.1:18787/health
```

再在浏览器打开 [公网健康检查](https://oscd.we1chan.com/health)，两处均应返回 `{"ok": true}`。
最后使用第一个终端打印的完整 `/join?code=...` 链接进入，再打开 `/present` 展示二维码。用手机流量扫码、上传、提交，确认 Qt 老板窗口出现同一订单。
如出现 502，先验证电脑本机的 `127.0.0.1:8787/health`，然后检查 SSH 隧道；完整故障处理见 [课堂演示说明](classroom-demo.md)。

配置依据 [Nginx 代理模块](https://nginx.org/en/docs/http/ngx_http_proxy_module.html)、[OpenSSH 反向转发](https://man.openbsd.org/ssh)、[Certbot 使用说明](https://eff-certbot.readthedocs.io/en/stable/using.html#webroot) 和 [阿里云证书下载说明](https://help.aliyun.com/zh/ssl-certificate/download-an-ssl-certificate)。
