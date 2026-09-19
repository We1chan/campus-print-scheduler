# 协作约定

## 开发流程

```sh
git switch main
git pull --ff-only
git switch -c feat/scheduler-aging
# 修改本模块，运行测试
git add <明确的文件路径>
git commit -m "feat(scheduler): add waiting-time aging"
git push -u origin feat/scheduler-aging
```

在 GitHub 开 PR，关联开发任务，写明行为变化与验收步骤；至少另一位成员评审，CI 三个平台通过后合并。完成后删除自己的已合并功能分支即可。不要强推 main。

初始化骨架可以由仓库管理员提交到 main；后续功能遵循上面的 PR 流程。

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
