# GitHub CI、NuGet 与 npm Trusted Publishing

[English](github-ci.md) | 中文

同一个 `v*` 发布标签现在也会发布 WASM npm 包，版本直接取自标签。
新增的 `npm` GitHub 环境和 npm Trusted Publisher 设置见[npm 发布说明](npm-publishing.zh-CN.md)。
两套验证都通过后才上传 NuGet/npm，GitHub Release 等两边发布成功后再创建。

## 已配置的流程

- `lint.yml`：分支 push、PR、手动运行和发布前检查。使用 actionlint 校验
  工作流；解析 PowerShell、JavaScript、Python、JSON、XML；按锁定的 SDK 和
  依赖构建，将编译器及分析器警告视为错误；执行 P4 JIT 断言。
  这里没有引入全仓库格式化，也不等同于 PSScriptAnalyzer、Ruff 或 ESLint。
- `publish.yml`：在 `TaiRuiQu/VelociTex` 推送 `v*` 版本标签时触发。
  校验版本标签和源提交，通过 Lint、P4 JIT/AOT、结果及导出一致性、重复打包、
  许可证和 SourceLink 检查后，再执行隔离的 Core/Direct2D JIT/AOT 包消费验证。
  最后单独的发布任务校验包哈希，通过 OIDC 换取短期 NuGet 凭据，上传三个包和符号包。
  NuGet 上传成功后，另一个任务自动创建带生成说明的 GitHub Release。
  `v1.0.0-rc.2` 自动标为预发布，`v1.0.0` 自动标为正式发布。

构建任务没有 OIDC 权限。发布任务不检出和执行仓库构建脚本，只消费本次运行的
不可变制品。使用的第三方 Actions 均锁定到提交哈希。
只有创建 GitHub Release 的任务拥有 `contents: write` 权限，该任务没有 OIDC 权限。
同一工作流完成两种发布，避免依赖 `GITHUB_TOKEN` 创建的 Release 再次触发工作流。
不同版本标签使用独立并发组，避免后推的标签替换前一个待运行版本。

## 首次启用需配置账户

1. GitHub 仓库 **Settings → Environments** 中创建 `nuget`，将可部署引用限制为
   预期的 `v*` 标签。可按需要配置 required reviewers；工作流本身没有强制人工审批。
2. 在该环境添加 Secret：`NUGET_USER`。值为 **nuget.org 个人资料用户名**，
   不是邮箱，也不是 API Key。
3. 在 nuget.org 登录该用户，创建 Trusted Publishing 策略：

   | 字段 | 值 |
   | --- | --- |
   | Repository owner | `TaiRuiQu` |
   | Repository | `VelociTex` |
   | Workflow file | `publish.yml`，仅文件名 |
   | Environment | `nuget` |
   | 包范围 | `VelociTex.*`，或三个准确的包 ID |

   首次发布需允许创建新包，之后需允许发布新版本。选择实际的包所有者（个人或组织），
   并确保登录用户有相应权限。首次发布前确认包名可用。无需存储长期 NuGet API Key。
   参见微软的 [Trusted Publishing 文档](https://learn.microsoft.com/en-us/nuget/nuget-org/trusted-publishing)。
4. 可在分支保护中要求两个 Lint 检查通过，并用仓库 ruleset 限制发布标签的创建者。

上述账户设置不会随 YAML 自动创建。添加 CI 配置本身不会发布包。
本次实际执行和未执行的检查见 [CI 验证记录](verification/ci-2026-10-19.md)。

## 如何发布

提交并推送审阅后的代码和工作流，使用新版本，例如 `v1.0.0-rc.2`。

```powershell
git tag v1.0.0-rc.2
git push origin v1.0.0-rc.2
```

推送标签后自动执行：Lint → 验证及打包 → NuGet Trusted Publish → 创建 GitHub Release。
支持轻量和附注标签；格式为 `v主版本.次版本.修订版本[-预发布后缀]`。
其他 `v*` 名称会校验失败，没有 `v` 前缀的标签不会触发此流程。
标签对应提交必须包含这些工作流。请逐个推送版本标签；GitHub 一次推送超过三个
标签时不会产生标签 push 事件。手动创建或编辑 Release 不会重复触发 NuGet 上传。

创建 Release 前会核对远端标签仍指向已验证提交。已存在且预发布状态匹配的公开 Release
会保留；草稿或状态冲突会报错。说明由 GitHub 自动生成，无需额外 PAT。
如果 NuGet 已成功、仅创建 Release 失败，请使用 **Re-run failed jobs**，避免重复上传包。

工作流保存六个包文件、打包报告、SourceLink 报告，以及运行报告、消费日志和本次
生成的消费锁。NuGet 上传不是事务：后一个包失败时，前面的包可能已上传。
重复版本会报错，不会静默跳过；重试前先处理部分发布，或使用新版本。

独立消费者通过新增的 `-LockDirectory` 参数，将本次包字节对应的锁写入忽略目录
`artifacts/release-consumer/locks`。生成锁后立即执行 locked restore，并校验真实包和
DLL 哈希。这些是运行时生成的锁，不能冒充已人工审阅的源锁。库的构建依赖始终使用
仓库已提交的锁；不会改写已冻结的 RC1 包或 `samples/**/package-locks`。

本地复现命令和详细边界见 [英文文档](github-ci.md)。CI 不替代独立浏览器差分、
真实 WinUI 截图验收，以及旧硬件、物理显示器 DPI、最低 Windows、GPU 和读屏音频验收。
正式 `1.0.0` 发布前仍须完成 [发布清单](release-checklist.md) 中的剩余项和 XML 注释清理。
