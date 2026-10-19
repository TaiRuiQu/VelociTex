# Agent 接续说明

[English](agent-handoff.md) | 中文

更新日期：2026-10-19，P4 与 P5 本地候选准备之后。本文是当前接续状态；原始设计轮工作
仅保留在[设计验证记录](verification/design-review.2026-10-19.zh-CN.md)，不作为下一步任务。

## 当前交付

P0～P4 已实现并提交：P2 `867283c`、P3 `cd05074`、P4 `1b0c777`。
本地 `1.0.0-rc.1` 候选来自干净的 P4 源版本
`1b0c77774418fd349c36b8b4cd0c89a135f70fba`，已有真实隔离 JIT/AOT、WinUI 和下游
消费的进程、包与截图证据。详见 [P4 记录](verification/p4-2026-10-19.md)、
[P5 记录](verification/p5-2026-10-19.md) 及 `docs/reference/p4.evidence.json`、`p5.evidence.json`。

范围内 2939 个注册身份都有实际执行的源码观察用例。P4 的 3659 个有效、50 个错误输入
在 JIT/AOT 每种运行时的 3843 场景中通过全部硬差分，11529 个普通导出字节一致。
注册用例覆盖不等于任意参数、宏组合和平台的穷尽证明。当前接口以
[包契约](package-contract.md) 和实际源码为准，不沿用早期架构中的目标 API 草图。

用户明确允许抗锯齿差异，以数学排版正确为标准。原始像素诊断仍保留；语义、几何、缺字、
错序、裁切和颜色丢失仍是硬失败。

## 必须保持的要求

- 固定 C#/.NET 10、WinUI 3、Windows x64、Native AOT/trimming；唯一数学基线为
  KaTeX 0.19.0 / `6ea2dc97188f0cc282e05a9690418ae7fe5457ba`，不跟踪 main 或升级。
- 用紧凑原生数据保留上游数学规则；运行时不建立 DOM，不依赖 JavaScript、浏览器、
  Node.js 或 Typst 数学执行链。
- 公式可用原版 KaTeX 字体，正文仍为非衬线；字体独立保留 OFL/RFN，不标成 MIT。
- Core 只处理单条完整公式。下游 DirectWrite 段落换行和 inline-object 宿主不成为
  Core 的段落职责。
- 保持原生显示、PNG/SVG/MathML、完整源公式复制和基础无障碍；UIA 属性不等于读屏语音。
- 完整缓存未命中编译的预热 P95 目标为 10 ms，包含 shaping 和独立结果分配。≥10× 是
  后续目标，当前未宣称达到；现代机数据不能替代旧 CPU 门槛。

## 正式发布剩余事项

本地候选准备完成；P5/1.0 正式退出仍待旧 CPU/低内存实机、真实显示器 DPI 来回切换、
最低 Windows 10 实机、硬件 GPU/设备失效和精确 native/GPU 峰值，以及屏幕阅读器语音。
SourceLink 语法和嵌入源 checksum 已验证，本地提交的远端可访问性未验证。没有推送或
对外发布授权。

冻结 RC1 另有 27 个 record 属性 XML 文档包含孤立 `>` 文本：XML 合法、summary 完整，
但正式版本前应规范源码注释格式并生成新候选。不得用同版本静默替换冻结包或沿用旧锁。

后续实现变化应继续保留数学硬差分、资源上限、JIT/AOT 消费与真实 WinUI 自动截图。
布局临时数组、独立结果和导出仍分配；管理内存及 native 开销必须如实记录。

## 复现与阅读

按“需求 → 包契约 → P4/P5 记录 → 验证 → 迁移”阅读。`verify-p4.ps1` 检查最终数学及参考，
`tools/Write-P5Evidence.ps1 -Check` 核对冻结候选证据。重新构建须用固定 SDK/工具链及
记录的干净源提交；P5 记录区分“核对已有产物”和“重建新候选”。大体积证据在忽略的
`artifacts/` 中。

## 下游及发布边界

触碰 `../oh-my-study/` 前先读其 AGENTS.md。迁移前已有大量用户改动；
`integration/oh-my-study/p3.patch`、`p5.patch` 只相对于各自已保存的 dirty baseline，
不是任意干净 checkout 的补丁。不得暂存或提交无关用户工作。公式字体例外已记入下游，
打印仍保留原 Typst 路径；每次 UI 修改必须自动截图并实际检查。

用户提供的仓库身份为 `TaiRuiQu/VelociTex`。本地打包、版本号或源映射不授权对外发布、
上传、推送或发布签名；没有执行的检查继续明确标记，不能写成已经完成。
