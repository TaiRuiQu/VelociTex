# WASM / JS 网页绑定

此预览版将现有 C# 数学核心编译为 .NET WASM AOT，通过独立 SVG 后端在网页绘制。
不需要 Blazor，也不要求访客安装 .NET。首次使用会下载随包提供的 WASM 运行时。
数学规则仍固定在 KaTeX 0.19.0。

`0.1.0-preview.2` 已发布到 npm 的 `next` 通道，包内 README 和 `images/`
包含四张原生 / WASM / KaTeX 0.19.0 渲染对比图，不包含 benchmark 代码或结果。

## 安装和调用

从 npm 安装预览通道：

```sh
npm install velocitex@next
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

所有入口返回 Promise，首次调用自动初始化并复用同一个运行时。HTML 包含 SVG 和
视觉隐藏的 MathML，SVG 内嵌字形轮廓，无需另行引入 CSS 或字体。
`fontSize` 是公式自身的 CSS 像素字号，默认 24。

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

- 浏览器尚无 Windows DirectWrite 字体回退；中文数学文字及其他依赖缺失字体或
  上下文塑形的输入返回 `UnknownSymbol`。普通正文中文不受影响。
- 这是浏览器端 API，`renderToString()` 也需要浏览器；尚未实现 Node.js SSR。
- 不包含 KaTeX contrib、HTML 注入、链接、外部图片、自动公式换行和公式编辑功能。
- 目前不声明比 KaTeX 更快。冷启动、下载大小、完整导出和预热渲染需分别衡量。
- 代码为 MIT；KaTeX 原字体及提取轮廓保留 OFL-1.1 / RFN，分发包包含独立许可证。

## 开发构建

CI 现已配置为与 NuGet 使用同一个 `v*` 发布标签，npm 版本取自标签；
首次使用需要完成[npm Trusted Publisher 设置](npm-publishing.zh-CN.md)。

```powershell
dotnet workload install wasm-tools --skip-manifest-update
./build-wasm.ps1 -Locked
cd npm/velocitex
npm pack --pack-destination ../../artifacts/wasm
```

使用仓库固定的 .NET SDK。默认发布 Release WASM AOT；`-NoAot` 仅用于开发，
`npm pack` 会拒绝把非 AOT 构建作为正式 npm 分发包。
字形轮廓已提交，普通构建无需 Python；只有重新生成/检查轮廓时才需要安装
`tools/VelociTex.DataGeneration/requirements-wasm.txt`。

新项目与原生解决方案分开：`VelociTex.Web.slnx`。浏览器示例见
[samples/VelociTex.Web.Sample](../samples/VelociTex.Web.Sample/README.md)。

安装开发依赖 `npm ci --prefix tools/reference` 后，运行
`./verify-wasm.ps1 -SkipBuild` 可重新验证本地打包、全新 npm 安装、TypeScript 类型和
真实浏览器中的全量差分；Windows 默认使用已安装的 Chrome，可通过
`VELOCITEX_BROWSER` 指定其他 Chromium 路径。
