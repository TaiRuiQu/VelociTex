# 需求与设计文档验证记录

[English](design-review.2026-10-19.md) | 中文

日期：2026-10-19。当前交付物仅为需求、设计、实施与交接资料；本文件不表示渲染引擎已实现。

## 已执行检查

| 检查 | 结果 |
| --- | --- |
| 本地 Markdown 链接、围栏配对、行尾空白 | 通过 |
| 两个 JSON 文件可解析，fixture ID 唯一 | 通过 |
| KaTeX v0.19.0 GitHub tag | 解析到 `6ea2dc97188f0cc282e05a9690418ae7fe5457ba` |
| npm 归档 | 在内存下载，重算 SHA-512 与 SHA-256，均与锁定资料相符 |
| 20 个 TTF | 文件名、长度和 SHA-256 全部相符；已检查 name 表许可证与保留名称 |
| 21 个有效公式种子 | 使用归档中的 dist/katex.js 0.19.0，renderToString 成功生成 HTML 与 MathML |
| 5 个无效 / 宏循环种子 | 均按预期抛出 KaTeX 解析错误，循环宏触发展开次数上限 |
| 3 个范围外种子 | 显式排除，不计为通过的兼容用例 |

语法校验时每条公式使用全新的宏对象，maxExpand=1000，displayMode 取 fixture 配置，output=htmlAndMathml，throwOnError=true。strict 处理器收集诊断并返回 ignore，因此这只证明样例可解析，不等于 strict 模式完整兼容。没有对导出的 MathML 做本库差分，因为本库尚未实现。

有效式覆盖微积分、3×3 矩阵、行列式、Maxwell 风格对齐式、cases、数组横线、伸缩箭头与花括号、字体、中文文本、原子间距、特殊盒、宏作用域、mathchoice、标签与核心 CD 环境。它们仍然只是种子集，不能代替 P4 要求的全量清单与语料。

输入资料摘要：

| 文件 | SHA-256 |
| --- | --- |
| `docs/examples/formulas.json` | `1061caae80c12af46a75b485de69305524eb6abb77ce1ccd4c135bab661b18a9` |
| `docs/reference/katex-0.19.0.lock.json` | `a742ad058752090b93db72ec9103d0c385d4a8cd34a35dae4ccf6088db5aa251` |

校验过程只在临时文件中加载经过摘要核验的 KaTeX bundle，结束后即删除；没有把浏览器、Node 依赖、上游代码或字体加入本仓库。

## 尚未执行

原生排版与截图对照、WinUI 宿主视觉、PNG / SVG 导出、本库 MathML、AOT 消费、旧 CPU 延迟、整页性能、内存压力和下游迁移均尚未执行，留给 P0 及后续阶段。本库是否达到 10ms 或 ≥10× 没有任何结论。

当时工作区尚非 Git 仓库，因此没有执行 `git diff --check`，也没有生成提交。文档检查基于文件内容；本轮未修改下游源码，也未发布任何包。

## 后续文档变更与复查

上述检查之后，文档集合做过一次结构调整：英文成为默认版本，英文文件去掉 `.en` 后缀（`README.md`、`AGENTS.md`、`docs/*.md`、`docs/verification/design-review.2026-10-19.md`），中文版本改为 `*.zh-CN.md`，并同步更新了跨语言链接与措辞。

变更后于同一日期复查：

| 检查 | 结果 |
| --- | --- |
| 全部 `.md` 的本地 Markdown 链接 | 通过，无失效目标 |
| 围栏配对与行尾空白 | 通过 |
| `docs/examples/formulas.json` 与 `docs/reference/katex-0.19.0.lock.json` 的 SHA-256 | 未变化，与上表一致 |
| `docs/examples/formulas.json` 的 fixture ID | 29 个且唯一：21 有效、5 无效、3 排除 |

未重新执行：npm 归档摘要核对、20 个 TTF 的摘要与 name 表检查，以及种子集的 KaTeX 渲染验证，因为上游与字体数据没有变化。
