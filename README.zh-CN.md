# VelociTex

现已新增可选 **WASM / JS 浏览器预览版**，复用 C# Core，提供 npm ES Module、
TypeScript 类型和 `await renderAll()`。参阅 [网页使用说明](docs/wasm-js.zh-CN.md)、
[npm 文档](npm/velocitex/README.md)和[验证记录](docs/verification/wasm-2026-10-19.md)。
目前可以构建及安装本地 npm 分发包，尚未发布到 npm；浏览器字体回退仍有限制。

[English](README.md) | 中文

面向原生 Windows 应用的 C# 数学排版与渲染库。目标是在 WinUI 3 上还原 **KaTeX 0.19.0 的核心数学排版**，导出 PNG / SVG / MathML，并优先替换 `../oh-my-study/` 的公式显示链路。

**截至 2026-10-19：P0～P4 实现已交付，P5 本地候选准备已完成。** `1.0.0-rc.1` 保持 KaTeX 0.19.0 基线，已有真实隔离的 JIT/AOT 与 WinUI 包消费者。P4 范围内 2939 个注册身份均有源码观察用例；3659 个有效和 50 个错误输入在每种运行时的 3843 场景中通过独立硬检查，下游原生显示已接入。用户允许抗锯齿差异，排版和裁切仍严格检查。详见 [P5 记录](docs/verification/p5-2026-10-19.md)。正式 1.0 退出仍待旧硬件、真实显示器 DPI、Windows 10 最低版本、硬件 GPU/设备和屏幕阅读器语音验收。网页绑定已独立发布为 npm 预览版 `velocitex@0.1.0-preview.2`，可用 `npm install velocitex@next` 安装，包内包含渲染对比图。

## 已确认的方向

- C# / .NET 10；首发 Windows 10/11 x64，并保持 Native AOT 兼容。
- 数学核心不依赖 WinUI；原生绘制使用系统 DirectWrite / Direct2D，允许使用轻量 C# 绑定。
- 只覆盖 KaTeX 0.19.0 核心数学，含用户宏与分组作用域；浏览器功能与扩展不在范围内。
- 公式允许使用原版 KaTeX 字体；可接受抗锯齿差异以及轻微的字体、间距偏差。
- 首版优先保证排版效果与接入成本：初始化完成且公式缓存未命中时，从校验单条公式到生成可绘制结果的 P95 ≤10ms。
- 之后持续优化，探索可复现的 ≥10× 加速；该倍数不是首版门槛。
- 以 MIT 许可证开源并独立发布 NuGet 包；随包字体保留其 OFL-1.1 许可证。

## 文档

| 文档 | 用途 |
| --- | --- |
| [需求与边界](docs/requirements.zh-CN.md) ([English](docs/requirements.md)) | 用户确认的目标、首版范围、兼容定义 |
| [架构与接口](docs/architecture.zh-CN.md) ([English](docs/architecture.md)) | 紧凑数据结构、流水线、内存所有权、渲染与导出 |
| [分阶段实施](docs/roadmap.zh-CN.md) ([English](docs/roadmap.md)) | 依赖、任务、阶段出口、预发布与正式版的区别 |
| [验证与性能](docs/validation.zh-CN.md) ([English](docs/validation.md)) | KaTeX 对照、视觉阈值、10ms 的精确定义、内存与加速测量 |
| [下游迁移](docs/integration-oh-my-study.zh-CN.md) ([English](docs/integration-oh-my-study.md)) | 接入位置、行内宿主、迁移与回退 |
| [Agent 交接](docs/agent-handoff.zh-CN.md) ([English](docs/agent-handoff.md)) | 如何开始、完成定义、已知风险 |
| [上游基线](docs/reference/katex-0.19.0.lock.json) | 已核验的提交、发行包摘要、20 个 TTF 摘要 |
| [公式种子集](docs/examples/formulas.json) | 微积分、矩阵、样式、宏、边界与错误用例 |
| [文档验证记录](docs/verification/design-review.2026-10-19.zh-CN.md) ([English](docs/verification/design-review.2026-10-19.md)) | 本轮实际检查范围：文档、上游摘要、公式样本 |

建议按“交接 → 需求 → 架构 → 实施 → 验证 → 迁移”的顺序阅读。本地候选和消费者已冻结；接续明确待执行的正式硬件、平台、语音门槛，以及候选 XML 注释瑕疵修订。

## 开发验证

GitHub Actions Lint 与 NuGet Trusted Publishing 的启用步骤见 [CI 与发布配置](docs/github-ci.zh-CN.md)。

```powershell
./verify-reference.ps1 -FullMatrix
./build.ps1 -Target All -Locked
./verify-p1.ps1
./build.ps1 -Target P2 -Locked
./verify-p2.ps1
./verify-p4.ps1
python tools/VelociTex.Native.Probe/verify-native.py artifacts/native
```

参考再生成需锁定的本机浏览器字节、Node/npm 和 Python；原生验证需固定 .NET SDK 与 Windows C++/AOT 工具链，普通库消费者不需要这些开发工具。大体积证据位于忽略的 `artifacts/`；小型散列清单、生成数据、字体、许可证和依赖锁提交到 Git。没有创建远程仓库或发布外部包。
