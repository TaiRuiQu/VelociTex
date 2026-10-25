# WASM / JS 网页绑定

浏览器适配器直接调用 C++ 核心编译出的 `velocitex-core.wasm`，输出与
**KaTeX 0.19.0 逐字节相同**的 HTML + CSS 与 MathML，并直接使用锁定的 KaTeX 样式表和
原字体。显式请求 SVG 或旧契约时回退到 .NET WASM AOT 数学核心。无需 Blazor，也不
要求访客安装 .NET。数学规则仍固定在 KaTeX 0.19.0。

浏览器包里没有 JavaScript 公式解析器，也没有 Rust 公式解析器：WebAssembly 模块就是
`native/core` 的 C++ 核心，与 .NET 端点共用同一份解析与渲染代码。

`tests/native/katex-html-parity.mjs` 与 `tests/native/katex-mathml-parity.mjs`
在原生侧逐字节校验 5,416 条公式的 HTML 与 MathML（真实世界语料、精选功能语料、
冻结的基准工作负载与仓库自带的 fixture 语料）；
`tests/wasm/katex-browser-parity.mjs` 在 Chromium 中对其中 1,651 条重复同样的逐字节
比对。在 36 条公式的浏览器基准中，C++ WebAssembly 核心比 KaTeX 0.19.0
**快 3.9 倍**（42.8 µs vs 165.8 µs/公式），完整数据与复现方法见
[性能基准](benchmarks.md)。

## 安装和调用

从 npm 安装：

```sh
npm install velocitex@1.0.0
```

Vite 等打包应用需要把运行时复制到静态资源目录：

```sh
npx velocitex-copy-assets public/velocitex
```

```html
<p>行内公式：\(E=mc^2\)</p>
<div data-math="\frac{a+b}{c}" data-display-mode="true"></div>
```

```js
import { init, renderAll } from 'velocitex';

await init({ assetBaseUrl: '/velocitex/' });
const { rendered, errors } = await renderAll();
```

应用部署在子路径时，`assetBaseUrl` 也应包含对应前缀。升级包后重新复制资源；
`dotnet.js` 和所有同目录文件必须来自同一次构建。需通过 HTTP(S) 提供这些文件，
`.wasm` 的 MIME 类型为 `application/wasm`，`.js` 为 JavaScript。
采用单线程构建，无需为此配置跨源隔离头。

直接托管分发包的 `dist/` 目录时，运行时默认从模块旁的 `runtime/` 自动加载，
可以只写 `import { renderAll } from './dist/index.mjs'; await renderAll();`。

## 单公式和字符串输出

```js
import { render, renderToString } from 'velocitex';

await render(String.raw`\sqrt{x^2+y^2}`, document.querySelector('#formula'));
const html = await renderToString('x^2');
const svg = await renderToString('x^2', { output: 'svg' });
const mathml = await renderToString('x^2', { output: 'mathml' });
```

所有入口返回 Promise，首次调用自动初始化并复用同一个运行时。默认 HTML 由
KaTeX 兼容的 WebAssembly 引擎生成；首次使用时自动注入随包分发的
`runtime/katex.min.css`（可用 `init({ injectStyles: false })` 关闭，并自行通过
`stylesheetUrl()` 获取地址）。设置 `fastHtml: false`（兼容旧名
`fastMathml: false`）或 `output: 'svg'` 使用 C# 输出：SVG 配合视觉隐藏的 MathML，
SVG 内嵌字形轮廓，无需另行引入 CSS 或字体。
`fontSize` 是公式自身的 CSS 像素字号，默认 24；KaTeX HTML 本身使用 em，字号由外层
CSS 控制。

`renderAll()` 默认处理 `[data-math]` 和 `$$…$$`、`\[…\]`、`\(…\)`。
普通 `$…$` 需要通过 `delimiters` 显式启用，以免误识别金额。它跳过代码、脚本、
输入区、可编辑区、`.velocitex-ignore` 和已经渲染的节点；可在动态追加内容后再次调用。
它不持续监听 DOM，也不跨不同文本节点匹配定界符。

`render()` / `renderToString()` 默认对非法公式抛出 `ParseError`，包含错误代码和
UTF-16 源范围。设置 `throwOnError: false` 后显示安全转义的原文。
`renderAll()` 默认显示错误原文并在返回值的 `errors` 中报告问题；基础设施错误仍会拒绝 Promise。
每次默认最多处理 1000 条公式，可通过 `maxFormulas` 调整，最高 10000。

支持字符串宏、行内/展示模式、严格模式、宏展开/输入/节点/嵌套预算、颜色和部分
KaTeX 数学选项，详见包内 TypeScript 声明。
宏定义独立于每条公式，不会隐式修改下一条公式的宏表。

## 当前边界

- KaTeX 兼容 HTML 路径使用未修改的 KaTeX 字体和样式表，包含 KaTeX 的 CJK /
  阿拉伯文等回退类；浏览器中 `\text{}` 的普通中文与 KaTeX 一致。旧版
  C# SVG 路径（`fastHtml: false`）没有 DirectWrite 塑形器，缺失字形返回
  `UnknownSymbol`。
- 默认 HTML 使用 KaTeX 的样式表和字体，因此必须加载 `runtime/katex.min.css`；
  快速路径会自动注入。`fastHtml: false` / `output: 'svg'` 不需要 CSS。
- 这是浏览器端 API，`renderToString()` 也需要浏览器；尚未实现 Node.js SSR。
- 不包含 KaTeX contrib、HTML 注入、链接、外部图片、自动公式换行和公式编辑功能。
- 代码为 MIT；KaTeX 原字体及提取轮廓保留 OFL-1.1 / RFN，分发包包含独立许可证。

浏览器示例见 [示例说明](../samples/VelociTex.Web.Sample/README.md)。
