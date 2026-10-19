# npm 自动发布设置

与现有 NuGet 发布使用同一个 `v*` 标签和 `.github/workflows/publish.yml`。
例如推送 `v1.0.0-rc.2` 后，NuGet 和 npm 都发布 `1.0.0-rc.2`。

| 发布标签 | npm 版本 | npm dist-tag | 用户安装 |
| --- | --- | --- | --- |
| `v1.0.0-rc.2` | `1.0.0-rc.2` | `next` | `npm install velocitex@next` |
| `v1.0.0` | `1.0.0` | `latest` | `npm install velocitex` |

无需手动改 npm 的版本号：CI 从标签取版本，写入忽略目录中的临时分发包和对应锁文件。
仓库内 `npm/velocitex/package.json`、`package-lock.json` 保持原样，也不会创建版本提交。
不带 `v` 的标签不会触发发布；版本格式、源码提交或标签指向不一致时会失败。

## 一次性设置

1. 确保 npm 上的 `velocitex` 包由你的账号维护。新包需要先完成一次首发，才能在包设置中
   配置 Trusted Publisher。可手动运行 **WASM and npm** 工作流，下载它输出的已验证
   `.tgz`；或使用本地 `./build-wasm.ps1 -Locked`、`./verify-wasm.ps1 -SkipBuild` 生成。
   在维护者自己的终端登录 npm 并发布一次预览版：

   ```powershell
   npm login
   npm publish ./velocitex-0.1.0-preview.1.tgz --access public --tag next --ignore-scripts
   ```

   首发后再创建新的发布标签，避免把已存在的 npm 版本再次上传。配置 CI 的过程中未执行
   这条发布命令。若包名被其他账号占用，先调整包身份与工作流中的一致性检查。
2. 在 GitHub 仓库 **Settings → Environments** 创建环境 **`npm`**，允许目标 `v*` 标签部署。
   保持现有 `nuget` 环境。是否配置人工审核由维护者决定；工作流本身不增加人工确认步骤。
3. 在 npm 的 `velocitex` 包 **Settings → Trusted Publisher** 中选择 **GitHub Actions**：

   | 字段 | 填写值 |
   | --- | --- |
   | Organization/user | `TaiRuiQu` |
   | Repository | `VelociTex` |
   | Workflow filename | **`publish.yml`**，仅文件名 |
   | Environment | **`npm`** |
   | Allowed actions | 显式允许 **`npm publish`** |

   **不要填 `wasm.yml`**：它负责构建和浏览器验证，真正发布在 `publish.yml`。
   当前 npm 新建信任配置默认允许 staged publishing；直接自动发布需另外允许
   `npm publish`。新建信任配置应在两天内完成首次成功的 OIDC 发布，否则需要重建配置。
   不需要在 GitHub 保存 `NPM_TOKEN`、npm 密码或长期写入令牌。

发布使用 GitHub-hosted Ubuntu runner、Node 22.23.2、单独固定安装的 npm 11.16.0，
并启用 `id-token: write` 和 provenance。开发/基线工具继续使用原来的 npm 10.9.8。
配置依据：[npm Trusted Publishing 文档](https://docs.npmjs.com/trusted-publishers/)。
首发限制的相关说明：[npm 官方文档仓库的问题记录](https://github.com/npm/documentation/issues/1926)。

## 发布操作

提交并推送已检查的源码和工作流，再推送一个新版本标签：

```powershell
git tag v1.0.0-rc.2
git push origin v1.0.0-rc.2
```

流程为：Lint → 原生和网页两套验证 → NuGet 与 npm 发布 → 创建 GitHub Release。
网页验证包含 AOT 构建、从实际 `.tgz` 全新安装、TypeScript 消费和真实浏览器全量差分。
发布任务只下载同一次运行的归档和摘要清单，复核 SHA-256、标签版本、干净源码提交、
AOT 构建信息及包身份，然后发布同一份 `.tgz`；不会重新构建或执行包内生命周期脚本。

普通分支推送和 PR 只构建验证，不会发布。仅手动编辑 GitHub Release 也不会上传新包。
两边 registry 不支持跨站事务：一边失败时，另一边可能已经成功。修复后使用
**Re-run failed jobs**，避免重跑已经成功的上传。重复版本会失败，不会静默覆盖。

GitHub 环境、npm 包和 Trusted Publisher 属于账号设置，YAML 不会替你创建。
实际托管执行、OIDC 交换、provenance 和 registry 上传仍需首次线上运行确认；本地检查见
[验证记录](verification/npm-ci-2026-10-19.md)。
