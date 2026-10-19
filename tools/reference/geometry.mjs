// Logical formula size comes from upstream's measured unbreakable bases, whose
// struts encode ascent/depth. A display .katex-html box is the container width,
// and an inline .katex-html box is a font line box: neither is formula size.
export function deriveFormulaGeometry(geometry) {
  const bases=geometry.nodes.filter(node=>node.classes.split(' ').includes('katex-base'));
  if(!bases.length)throw new Error('No measured KaTeX base for nonempty fixture');
  const struts=bases.map(base=>geometry.nodes.find(node=>node.index===base.index+1&&node.classes.split(' ').includes('katex-strut')));
  if(struts.some(x=>!x))throw new Error('Missing direct first-child KaTeX base strut');
  const left=Math.min(...bases.map(x=>x.x)),top=Math.min(...struts.map(x=>x.y));
  const right=Math.max(...bases.map(x=>x.x+x.width)),bottom=Math.max(...struts.map(x=>x.y+x.height));
  return {
    ...geometry,
    htmlBox:geometry.htmlBox??geometry.logical,
    logical:{x:left,y:top,width:right-left,height:bottom-top},
    advanceDip:bases.reduce((total,x)=>total+x.width,0),
    ascentDip:geometry.baselineDip-top,
    depthDip:bottom-geometry.baselineDip,
    tagBoxes:geometry.nodes.filter(node=>node.classes.split(' ').includes('katex-tag')).map(({x,y,width,height})=>({x,y,width,height})),
    logicalMeasurement:'Horizontal extent / advance from actual .katex-base rectangles; natural vertical extent from their direct first-child .katex-strut rectangles, excluding CSS inline line-box leading. Separate .katex-tag boxes. Corrected in P1; all values derive from retained original P0 raw DOM measurements.',
  };
}
