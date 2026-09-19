# 协作约定

## 开发流程

```sh
git switch main
git pull --ff-only
# 修改本模块，运行测试
git add <明确的文件路径>
git commit -m "feat(scheduler): add waiting-time aging"
git pull --rebase origin main
git push origin main
```

直接在 `main` 开发，按可运行增量及时 commit 和 push，不要求创建功能分支或 PR。提交中关联对应 Issue，写明行为变化和实际验证情况。

三人并行开发时，开始工作前先拉取，推送前再次同步。如果远端有新提交，完成 rebase 并解决冲突，再运行受影响的检查后推送。不要强推 `main`，不要覆盖其他成员的改动。推送后查看三平台 CI，失败及时修复；公共接口仍需与相关成员沟通。

## 提交身份

每个人在自己的机器上配置自己的姓名和 GitHub 邮箱，不共享用户身份、SSH 私钥或 token。Git 的提交身份与 GitHub 的登录认证是两件事。

```sh
git config user.name "你的 GitHub 用户名"
git config user.email "你的 GitHub noreply 邮箱"
git var GIT_AUTHOR_IDENT
```

## 模块边界

所有人先读 `docs/team.md` 和 `docs/interfaces.md`。避免以“顺便格式化”为由改他人模块；公共接口、顶层 CMake、CI 的变动保持小而明确。

先写行为测试，再实现高风险并发/恢复逻辑。修正文档和样式无需机械增加测试。所有断言必须在 Release 构建有效。

## 跨平台必查

- 不提交 build、Qt SDK、IDE 用户配置、绝对机器路径。
- 不在业务层使用 `system()`、`sleep` shell 命令、`kill` shell 命令或硬编码 `.exe`。
- 头文件大小写与真实文件名完全一致，Linux CI 会发现大小写问题。
- 统一 UTF-8/LF；Windows/MSVC 开启 `/utf-8`。
- 改动涉及文件或子进程时，用中文 + 空格路径验证。
- 不依赖主机时区排序，业务时间统一定义；测试尽量使用可控时钟。

## 代码复用与课程诚信

报告中区分初始化骨架、成员实现、第三方库和外部数据来源。每个成员应能解释自己提交的代码及测试。不要把未运行的实验或 CI 写成已通过。
