import velocitex, {
  init,
  render,
  renderToString,
  renderAll,
  ParseError,
  type RenderOptions,
  type RenderAllResult,
} from 'velocitex';

const options: RenderOptions = { displayMode: true, macros: { '\\RR': '\\mathbb{R}' }, strict: 'warn' };
await init({ assetBaseUrl: '/velocitex/' });
await render('x', document.body, options);
const svg: string = await renderToString('x', { output: 'svg' });
const report: RenderAllResult = await renderAll(document.body, {
  delimiters: [{ left: '$', right: '$', display: false }],
});
await velocitex.renderAll(document.createDocumentFragment());
try {
  await renderToString(svg);
} catch (error) {
  if (error instanceof ParseError) console.log(error.code, error.range.start);
}
console.log(report.rendered);
