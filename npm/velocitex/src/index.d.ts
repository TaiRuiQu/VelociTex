export interface InitOptions {
  /** Directory containing dotnet.js and its sibling runtime assets. Defaults to ./runtime/ beside the module. */
  assetBaseUrl?: string | URL;
}
export interface RenderOptions {
  displayMode?: boolean;
  /** Formula em size in CSS pixels. Default 24. */
  fontSize?: number;
  macros?: Record<string, string>;
  /** Default true for render/renderToString; false for renderAll. */
  throwOnError?: boolean;
  errorColor?: string;
  /** html includes SVG plus visually hidden semantic MathML. Default html. */
  output?: 'html' | 'svg' | 'mathml';
  strict?: 'ignore' | 'warn' | 'error' | boolean;
  maxExpand?: number;
  maxInputLength?: number;
  maxNodes?: number;
  maxNesting?: number;
  maxTokens?: number;
  maxSize?: number;
  minRuleThickness?: number;
  colorIsTextColor?: boolean;
  leqno?: boolean;
  fleqn?: boolean;
  displayWidth?: number;
}
export interface Delimiter { left: string; right: string; display: boolean; }
export interface RenderAllOptions extends RenderOptions {
  /** Default [data-math]. The attribute or the matched element's text supplies TeX. */
  selector?: string;
  /** Defaults to $$...$$, \[...\], \(...\). Single-dollar math is opt-in. */
  delimiters?: Delimiter[];
  /** Per-call bound, default 1000, maximum 10000. */
  maxFormulas?: number;
}
export class ParseError extends Error {
  constructor(source: string, diagnostic: { code: string; message: string; start: number; length: number });
  readonly source: string;
  readonly code: string;
  readonly range: { start: number; length: number };
}
export interface RenderAllResult {
  rendered: number;
  errors: Array<{ source: string; error: ParseError; element: Element }>;
}
export function init(options?: InitOptions): Promise<void>;
export function render(source: string, element: Element, options?: RenderOptions): Promise<void>;
export function renderToString(source: string, options?: RenderOptions): Promise<string>;
export function renderAll(root?: Element | Document | DocumentFragment, options?: RenderAllOptions): Promise<RenderAllResult>;
declare const velocitex: { init: typeof init; render: typeof render; renderToString: typeof renderToString; renderAll: typeof renderAll; ParseError: typeof ParseError };
export default velocitex;
