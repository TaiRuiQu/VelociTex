Box Layout::BuildBox(std::span<const SemanticNode> nodes, const SemanticNode& node, LayoutContext context, const MathOptions& options, std::vector<Box>& boxes, std::vector<double>& x, std::vector<double>& y, std::optional<Box> composed){ {
Box body = value_or(composed, boxes[node.FirstChild]);
Box result = body;
result.FirstDecoration = 0;
result.Symbol = false;
result.TextShaped = false;
x[node.FirstChild] = y[node.FirstChild] = 0;
do {
if(node.Text == u"\\mathllap") {
x[node.FirstChild] = -body.Width;
result.Width = 0;
result.Atom = MathAtom::Ord;
break;
} else if(node.Text == u"\\mathclap") {
x[node.FirstChild] = (-body.Width / 2);
result.Width = 0;
result.Atom = MathAtom::Ord;
break;
} else if(node.Text == u"\\mathrlap" || node.Text == u"\\vphantom") {
result.Width = 0;
result.Atom = MathAtom::Ord;
break;
} else if(node.Text == u"\\smash") {
std::u16string selection = value_or(node.Alignment, u"tb");
bool valid = true;
for(auto c : selection) if(!((((c == u't') || (c == u'b'))))) {
valid = false;
break;
}
if((valid && contains(selection, u't'))) result.Height = 0;
if((valid && contains(selection, u'b'))) result.Depth = 0;
result.Atom = MathAtom::Ord;
result.Character = false;
break;
} else if(node.Text == u"\\raisebox") {
double shift = Length(node.Length1, context, options);
y[node.FirstChild] = -shift;
result.Height += shift;
result.Depth -= shift;
result.Character = false;
result.NoAtom = true;
break;
} else if(node.Text == u"\\vcenter") {
double dy = ((((body.Height - body.Depth)) / 2) - (MathConstants::axisHeight(context.Metrics) * context.Scale));
y[node.FirstChild] = dy;
result.Height -= dy;
result.Depth += dy;
result.Character = false;
result.NoAtom = true;
break;
} else if(node.Text == u"\\pmb") {
result.Atom = node.Atom;
result.Character = false;
break;
} else if(node.Text == u"\\reflectbox" || node.Text == u"\\mathreflectbox") {
result.Atom = MathAtom::Ord;
result.Character = false;
break;
} else if(node.Text == u"cdleft" || node.Text == u"cdright") {
result.Width = result.Height = result.Depth = 0;
double labelScale = context.WithStyle(SupStyles[context.Style]).Scale;
x[node.FirstChild] = ((node.Text == u"cdleft") ? (-body.Width - (.3 * labelScale)) : (.3 * labelScale));
double lineDescent = (((GlyphInkData::FontDescents[8] + .1)) * labelScale);
y[node.FirstChild] = ((((body.Depth * labelScale) / context.Scale) - (.8 * labelScale)) - lineDescent);
break;
} else if(node.Text == u"cdparent") {
for(int child = nodes[node.FirstChild].FirstChild;(child >= 0);child = nodes[child].NextSibling) if(((nodes[child].Kind == NodeKind::Box) && ((nodes[child].Text == u"cdleft") || (nodes[child].Text == u"cdright")))) {
x[child] = (body.Width / 2);
y[child] = body.Depth;
}
break;
}
} while(false);
return result;
}}
Box Layout::BuildVerb(const SemanticNode& node, LayoutContext context, std::vector<Decoration>& decorations){ int glyph{};TexGlyphMetric metric{};{
Box result = ([&]{Box v{};v.Atom=MathAtom::Ord;return v;}());
LayoutContext textContext = context.WithStyle(((context.Style < 2) ? (uint8_t)(0) : (uint8_t)(2)));
double scale = textContext.Scale;
std::u16string text = replace((value_or(node.Text2, value_or(node.Text, u""))), u' ',((node.Value != 0) ? u'\u2423' : u'\u00a0'));
for(auto c : text) {
int cp = ((c == 160) ? 32 : c);
if(!Find(19, cp, glyph, metric)) throw CompileFailure{MathDiagnostic{MathDiagnosticCode::UnknownSymbol,node.Range,u"The verbatim character has no locked typewriter glyph."}};
AddGlyph(result, 19, glyph, cp, result.Width, 0, scale, decorations);
result.Width += ((FontData::Glyphs[glyph].AdvanceUnits / (double)(FontData::Fonts[19].UnitsPerEm)) * scale);
result.Height = Math::Max(result.Height,(metric.Height * scale));
result.Depth = Math::Max(result.Depth,(metric.Depth * scale));
}
return result;
}}
Box Layout::BuildEnclose(std::span<const SemanticNode> nodes, const SemanticNode& node, LayoutContext context, const MathOptions& options, std::vector<Box>& boxes, std::vector<double>& x, std::vector<double>& y, std::vector<Decoration>& decorations){ MathColor borderColor{};MathColor background{};{
Box body = boxes[node.FirstChild];
double scale = context.Scale;
double rule = (Math::Max(MathConstants::defaultRuleThickness(context.Metrics),options.MinRuleThickness) * scale);
std::u16string label = value_or(node.Text, u"");
bool cancel = (((label == u"\\cancel") || (label == u"\\bcancel")) || (label == u"\\xcancel"));
Box result = ([&]{Box v{};v.Width=body.Width;v.Height=body.Height;v.Depth=body.Depth;v.Atom=MathAtom::Ord;v.DecorationsAfter=!node.Text2;return v;}());
x[node.FirstChild] = y[node.FirstChild] = 0;
if(cancel) {
double padding = (body.Character ? 0 : (.2 * scale));
double vertical = (body.Character ? (.2 * scale) : 0);
double left = -padding, top = (-body.Height - vertical), width = (body.Width + (2 * padding)), height = ((body.Height + body.Depth) + (2 * vertical));
if(((label == u"\\cancel") || (label == u"\\xcancel"))) AddStroke(result, 0, height, width, 0, (.046 * scale), decorations, left, top, options.FontSize);
if(((label == u"\\bcancel") || (label == u"\\xcancel"))) AddStroke(result, 0, 0, width, height, (.046 * scale), decorations, left, top, options.FontSize);
return result;
}
if((label == u"\\sout")) {
AddRule(result, 0, (((-.5 * MathConstants::xHeight(context.Metrics)) * scale) - rule), body.Width, rule, decorations);
return result;
}
if((label == u"\\phase")) {
double lineWeight = Length(TexLength{.6,TexUnit::Pt}, context, options), clearance = Length(TexLength{.35,TexUnit::Ex}, context, options);
double height = (((body.Height + body.Depth) + lineWeight) + clearance), padding = ((height / 2) + lineWeight);
result.Width += padding;
x[node.FirstChild] = padding;
double baseScale = Scale(context.Style), viewHeight = Math::Floor(((1000 * height) / baseScale));
std::vector<MathPathSegment> phase = std::vector<MathPathSegment>{MathPathSegment{MathPathOperation::Move,400000,viewHeight},MathPathSegment{MathPathOperation::Line,0,viewHeight},MathPathSegment{MathPathOperation::Line,(viewHeight / 2),0},MathPathSegment{MathPathOperation::Line,((viewHeight / 2) + 65),45},MathPathSegment{MathPathOperation::Line,145,(viewHeight - 80)},MathPathSegment{MathPathOperation::Line,400000,(viewHeight - 80)},MathPathSegment{MathPathOperation::Close,0,0}};
double pathScale = Math::Max((result.Width / 400000),(height / viewHeight));
AddPath(result, phase, 0, -body.Height, pathScale, pathScale, MathBounds{0,-body.Height,result.Width,height}, decorations);
result.Depth += (lineWeight + clearance);
return result;
}
bool angl = (label == u"\\angl"), colorBox = (label == u"\\colorbox"), framed = (((label == u"\\fbox") || (label == u"\\fcolorbox")) || (label == u"\\boxed"));
double thickness = (framed ? (Math::Max(MathConstants::fboxrule(context.Metrics),options.MinRuleThickness) * scale) : rule);
double horizontal = (angl ? (.03889 * scale) : (.3 * scale));
double topPadding = (angl ? (4 * rule) : ((.3 * scale) + ((colorBox ? 0 : thickness))));
double bottomPadding = (angl ? Math::Max(0,((.25 * scale) - body.Depth)) : topPadding);
result.Width += (horizontal * 2);
result.Height += topPadding;
result.Depth += bottomPadding;
x[node.FirstChild] = horizontal;
std::optional<MathColor> border = (try_parse_color(text(node.Text3), borderColor) ? std::optional<MathColor>(borderColor) : std::nullopt);
if((framed || angl)) {
double total = (result.Height + result.Depth);
AddRule(result,0,-result.Height,result.Width,thickness,decorations,false,false,border);
AddRule(result,(result.Width - thickness),-result.Height,thickness,total,decorations,true,false,border);
if(!angl) {
AddRule(result,0,(result.Depth - thickness),result.Width,thickness,decorations,false,false,border);
AddRule(result,0,-result.Height,thickness,total,decorations,true,false,border);
}
}
if(try_parse_color(text(node.Text2), background)) AddRule(result,0,-result.Height,result.Width,(result.Height + result.Depth),decorations,false,true,background);
return result;
}}
void Layout::AddStroke(Box& result, double x1, double y1, double x2, double y2, double thickness, std::vector<Decoration>& decorations, double offsetX, double offsetY, double coordinateScale){ {
double length = Math::Sqrt(((((x2 - x1)) * ((x2 - x1))) + (((y2 - y1)) * ((y2 - y1)))));
if((length == 0)) return;
double dx = (((-((y2 - y1)) / length) * thickness) / 2), dy = (((((x2 - x1)) / length) * thickness) / 2);
std::vector<MathPathSegment> path = std::vector<MathPathSegment>{MathPathSegment{MathPathOperation::Move,(x1 + dx),(y1 + dy)},MathPathSegment{MathPathOperation::Line,(x2 + dx),(y2 + dy)},MathPathSegment{MathPathOperation::Line,(x2 - dx),(y2 - dy)},MathPathSegment{MathPathOperation::Line,(x1 - dx),(y1 - dy)},MathPathSegment{MathPathOperation::Close,0,0}};
if((coordinateScale != 1)) for(int i = 0;(i < (int)path.size());i++) path[i] = ([&]{auto copy = path[i];copy.X1=(path[i].X1 * coordinateScale);copy.Y1=(path[i].Y1 * coordinateScale);return copy;}());
double left = (Math::Min(x1,x2) - thickness), top = (Math::Min(y1,y2) - thickness);
AddPath(result, path, offsetX, offsetY, (1 / coordinateScale), (1 / coordinateScale), MathBounds{(left + offsetX),(top + offsetY),(Math::Abs((x2 - x1)) + (2 * thickness)),(Math::Abs((y2 - y1)) + (2 * thickness))}, decorations);
}}
Box Layout::BuildRow(std::span<const SemanticNode> nodes, int id, LayoutContext style, std::vector<LayoutContext>& styles, std::vector<Box>& boxes, std::vector<double>& x, std::vector<double>& y, std::vector<int>& sequence, std::vector<int>& collectStack, std::vector<MathAtom>& atoms, const MathOptions& options){ {
const SemanticNode& node = nodes[id];
int length = Collect(nodes, node.FirstChild, sequence, collectStack, styles);
for(int i = 0;(i < length);i++) atoms[i] = boxes[sequence[i]].Atom;
for(int i = 1;(i < length);i++) {
int before = sequence[(i - 1)], current = sequence[i];
if(((((((((nodes[before].Kind == NodeKind::Symbol) && (nodes[current].Kind == NodeKind::Symbol)) && (nodes[before].Atom == MathAtom::Bin)) && (nodes[current].Atom == MathAtom::Bin)) && (nodes[before].Font == MathFont::AMS)) && (nodes[current].Font == MathFont::AMS)) && (boxes[before].Skew == boxes[current].Skew)) && (styles[before] == styles[current]))) atoms[i] = atoms[(i - 1)];
}
int previous = -1;
for(int i = 0;(i < length);i++) {
if((nodes[sequence[i]].Kind == NodeKind::Space)) continue;
if((((((((((i > 0) && (nodes[sequence[i]].Kind == NodeKind::Symbol)) && (nodes[sequence[(i - 1)]].Kind == NodeKind::Symbol)) && (nodes[sequence[i]].Atom == MathAtom::Bin)) && (nodes[sequence[(i - 1)]].Atom == MathAtom::Bin)) && (nodes[sequence[i]].Font == MathFont::AMS)) && (nodes[sequence[(i - 1)]].Font == MathFont::AMS)) && (boxes[sequence[i]].Skew == boxes[sequence[(i - 1)]].Skew)) && (styles[sequence[i]] == styles[sequence[(i - 1)]]))) {
atoms[i] = atoms[(i - 1)];
continue;
}
MathAtom current = atoms[i];
if((((previous >= 0) && (atoms[previous] == MathAtom::Bin)) && CancelsBinOnRight(current))) atoms[previous] = MathAtom::Ord; else if(((current == MathAtom::Bin) && (((previous < 0) || CancelsBinOnLeft(atoms[previous]))))) atoms[i] = MathAtom::Ord;
previous = i;
}
if(((previous >= 0) && (atoms[previous] == MathAtom::Bin))) atoms[previous] = MathAtom::Ord;
LayoutContext glueStyle = style;
if((((node.FirstChild >= 0) && (nodes[node.FirstChild].NextSibling < 0)) && (nodes[node.FirstChild].Kind == NodeKind::Style))) glueStyle = styles[node.FirstChild];
double mu = ((MathConstants::quad(MetricIndex(glueStyle)) * Scale(glueStyle)) / 18);
Box box = ([&]{Box v{};v.Atom=MathAtom::Ord;return v;}());
previous = -1;
for(int i = 0;(i < length);i++) {
int child = sequence[i];
const Box& childBox = boxes[child];
bool space = (nodes[child].Kind == NodeKind::Space);
if((previous >= 0)) {
int before = sequence[previous];
if((((((((((((nodes[before].Kind == NodeKind::Symbol) && (nodes[child].Kind == NodeKind::Symbol)) && (nodes[before].Font == nodes[child].Font)) && !((((nodes[child].Font == MathFont::Default) || (nodes[child].Font == MathFont::MathNormal))))) && (nodes[before].TextMode == nodes[child].TextMode)) && (nodes[before].IsMathOrd == nodes[child].IsMathOrd)) && (boxes[before].Font == childBox.Font)) && (boxes[before].Skew == childBox.Skew)) && (atoms[previous] == MathAtom::Ord)) && (atoms[i] == MathAtom::Ord)) && (styles[before] == styles[child]))) box.Width -= Math::Max(0,boxes[before].Italic);
}
if((((!space && (previous >= 0)) && !boxes[sequence[previous]].NoAtom) && !childBox.NoAtom)) box.Width += (Spacing(atoms[previous], atoms[i], (styles[child] >= 4)) * mu);
x[child] = box.Width;
y[child] = 0;
box.Width += childBox.Width;
box.Height = Math::Max(box.Height,childBox.Height);
box.Depth = Math::Max(box.Depth,childBox.Depth);
if(!space) previous = i;
}
if(((length == 1) && (node.Kind == NodeKind::Row))) box.Character = boxes[sequence[0]].Character;
bool hasNewLine = false;
if((id == 0)) for(int i = 0;(i < length);i++) if((((nodes[sequence[i]].Kind == NodeKind::Space) && (nodes[sequence[i]].Text == u"\\\\")) && ((!options.DisplayMode || (options.StrictMode != MathStrictMode::Error))))) {
hasNewLine = true;
break;
}
if(hasNewLine) {
bool content = false;
for(int i = 0;(i < length);i++) if((nodes[sequence[i]].Kind != NodeKind::Space)) {
content = true;
break;
}
hasNewLine = content;
}
if(hasNewLine) {
double maxWidth = 0, baseline = 0, previousDepth = 0;
int lineStart = 0;
bool firstLine = true;
for(int end = 0;(end <= length);end++) {
if(((end < length) && !((((nodes[sequence[end]].Kind == NodeKind::Space) && (nodes[sequence[end]].Text == u"\\\\")) && ((!options.DisplayMode || (options.StrictMode != MathStrictMode::Error))))))) continue;
double h = 0, d = 0, w = 0;
for(int i = lineStart;(i < end);i++) {
int child = sequence[i];
h = Math::Max(h,boxes[child].Height);
d = Math::Max(d,boxes[child].Depth);
w = Math::Max(w,((x[child] + boxes[child].Width) - (((lineStart < end) ? x[sequence[lineStart]] : 0))));
}
if(!firstLine) baseline += Math::Max((1.2 * style.Scale),(previousDepth + h));
for(int i = lineStart;(i < end);i++) y[sequence[i]] = baseline;
if((lineStart < end)) {
double origin = x[sequence[lineStart]];
for(int i = lineStart;(i < end);i++) x[sequence[i]] -= origin;
}
if(firstLine) {
box.Height = h;
firstLine = false;
}
box.Depth = (baseline + d);
maxWidth = Math::Max(maxWidth,w);
previousDepth = d;
if((end < length)) baseline += Length(nodes[sequence[end]].Length1, style, options);
lineStart = (end + 1);
}
box.Width = maxWidth;
}
return box;
}}
Box Layout::BuildOperator(std::span<const SemanticNode> nodes, const SemanticNode& node, bool centered, LayoutContext style, std::vector<Box>& boxes, std::vector<Decoration>& decorations, std::optional<Box> composed){ int glyphIndex{};TexGlyphMetric metric{};{
if((node.CodePoint == 0)) {
Box body = value_or(composed, (((node.FirstChild < 0) ? Box{} : boxes[node.FirstChild])));
body.Atom = MathAtom::Op;
body.Character = false;
body.Symbol = false;
body.FirstDecoration = 0;
if(((((node.Text == u"\\mathop") && (node.Value == 0)) && !composed) && (node.FirstChild >= 0))) {
int symbol = node.FirstChild;
while(((((nodes[symbol].Kind == NodeKind::Row) || (nodes[symbol].Kind == NodeKind::Font)) && (nodes[symbol].FirstChild >= 0)) && (nodes[nodes[symbol].FirstChild].NextSibling < 0))) symbol = nodes[symbol].FirstChild;
if(boxes[symbol].Symbol) {
body.GlyphY = ((((body.Height - body.Depth)) / 2) - (MathConstants::axisHeight(style.Metrics) * style.Scale));
body.Italic = boxes[symbol].Italic;
}
}
return body;
}
int font = (((style < 2) && (node.Text != u"\\smallint")) ? 16 : 15);
int cp = ((node.CodePoint == 0x222f) ? 0x222c : ((node.CodePoint == 0x2230) ? 0x222d : node.CodePoint));
if(!Find(font, cp, glyphIndex, metric)) throw CompileFailure{MathDiagnostic{MathDiagnosticCode::UnsupportedCommand,node.Range,u"This operator requires a missing locked glyph."}};
double scale = Scale(style), italic = (metric.ItalicCorrection * scale);
Box result = ([&]{Box v{};v.Width=((((FontData::Glyphs[glyphIndex].AdvanceUnits / (double)(FontData::Fonts[font].UnitsPerEm))) * scale) + Math::Max(0,italic));v.Height=(metric.Height * scale);v.Depth=(metric.Depth * scale);v.Italic=italic;v.Font=font;v.GlyphIndex=glyphIndex;v.GlyphScale=scale;v.GlyphY=((((((metric.Height - metric.Depth)) / 2) - MathConstants::axisHeight(MetricIndex(style)))) * scale);v.Symbol=true;v.Atom=MathAtom::Op;return v;}());
if(((node.CodePoint == 0x222f) || (node.CodePoint == 0x2230))) {
result.DecorationsAfter = true;
bool large = (font == 16), triple = (node.CodePoint == 0x2230);
double width = (((triple ? (large ? 1.98 : 1.304) : (large ? 1.472 : .957))) * scale);
double height = (((large ? .659 : .499)) * scale);
double top = ((((large ? (.08 * scale) : 0)) + result.GlyphY) - height);
double left = (centered ? (((result.Width - width)) / 2) : 0);
AddPath(result, MathPathCache::Get((std::u16string(triple ? u"oiiintSize" : u"oiintSize") + (large ? u"2" : u"1"))), left, top, (.001 * scale), (.001 * scale), MathBounds{left,top,width,height}, decorations);
}
return result;
}}
bool Layout::UseLimits(const SemanticNode& node, LayoutContext style) { return ((node.Text == u"\\operatorname") ? ((node.Text2 == u"star") && (((style.Style < 2) || (node.AlwaysLimits && !node.NeverLimits)))) : (!node.NeverLimits && ((node.AlwaysLimits || (node.Limits && (style.Style < 2)))))); }
Box Layout::BuildLimits(const SemanticNode& node, Box basis, Box sup, Box sub, LayoutContext style, std::vector<Box>& boxes, std::vector<double>& x, std::vector<double>& y){ {
basis.Height = Math::Max(0,basis.Height);
basis.Depth = Math::Max(0,basis.Depth);
double scale = Scale(style);
int m = MetricIndex(style);
double slant = basis.Italic, shift = basis.GlyphY;
bool hasSup = (node.SecondChild >= 0), hasSub = (node.ThirdChild >= 0);
double supKern = Math::Max((MathConstants::bigOpSpacing1(m) * scale),((MathConstants::bigOpSpacing3(m) * scale) - sup.Depth));
double subKern = Math::Max((MathConstants::bigOpSpacing2(m) * scale),((MathConstants::bigOpSpacing4(m) * scale) - sub.Height));
double width = Math::Max(basis.Width,Math::Max((hasSup ? (sup.Width + slant) : 0),(hasSub ? (sub.Width - slant) : 0)));
double spacer = ((hasSub && !boxes[node.ThirdChild].Character) ? slant : 0);
Box result = ([&]{Box v{};v.Width=(width + spacer);v.Height=(basis.Height - shift);v.Depth=(basis.Depth + shift);v.Atom=MathAtom::Op;return v;}());
x[node.FirstChild] = (spacer + (((width - basis.Width)) / 2));
y[node.FirstChild] = 0;
if(hasSup) {
x[node.SecondChild] = (spacer + ((((width - sup.Width) + slant)) / 2));
y[node.SecondChild] = (((-basis.Height + shift) - supKern) - sup.Depth);
result.Height = Math::Max(result.Height,((sup.Height - y[node.SecondChild]) + (MathConstants::bigOpSpacing5(m) * scale)));
}
if(hasSub) {
x[node.ThirdChild] = (spacer + ((((width - sub.Width) - slant)) / 2));
y[node.ThirdChild] = (((basis.Depth + shift) + subKern) + sub.Height);
result.Depth = Math::Max(result.Depth,((sub.Depth + y[node.ThirdChild]) + (MathConstants::bigOpSpacing5(m) * scale)));
}
return result;
}}
double Layout::SizeHeight(int size) { return ([&]() -> double { auto sw0 = size; if((sw0 == 1)) return 1.2; if((sw0 == 2)) return 1.8; if((sw0 == 3)) return 2.4; if(true) return 3; throw std::logic_error("exhaustive switch"); }()); }
bool Layout::AlwaysStacks(int cp) { return ((((((((((((((cp == u'|') || (cp == 0x2223)) || (cp == 0x2225)) || (cp == 0x2016)) || (cp == 0x2191)) || (cp == 0x2193)) || (cp == 0x2195)) || (cp == 0x21d1)) || (cp == 0x21d3)) || (cp == 0x21d5)) || (cp == 0x27ee)) || (cp == 0x27ef)) || (cp == 0x23b0)) || (cp == 0x23b1)); }
bool Layout::NeverStacks(int cp) { return ((((cp == 0x27e8) || (cp == 0x27e9)) || (cp == u'/')) || (cp == u'\\')); }
Box Layout::MakeDelimiter(int codePoint, double requiredHeight, LayoutContext style, bool center, int explicitSize, MathAtom atom, std::vector<Decoration>& decorations){ TexGlyphMetric small{};TexGlyphMetric large{};int glyphIndex{};TexGlyphMetric metric{};{
if(((codePoint == 0) || (codePoint == u'.'))) return ([&]{Box v{};v.Width=((explicitSize > 0) ? 0 : .12);v.Atom=atom;return v;}());
int cp = ([&]() -> int { auto sw1 = codePoint; if((sw1 == u'<')) return 0x27e8; if((sw1 == u'>')) return 0x27e9; if((sw1 == u'|')) return 0x2223; if((sw1 == 0x2016)) return 0x2225; if(true) return codePoint; throw std::logic_error("exhaustive switch"); }());
double scale = Scale(style);
int font = 8;
double glyphScale = 1;
bool stack = false;
if((explicitSize > 0)) {
font = (14 + explicitSize);
stack = AlwaysStacks(cp);
} else {
std::vector<double> smallScales = std::vector<double>{.5,.7,1};
int start = ((style >= 6) ? 0 : ((style >= 4) ? 1 : 2));
bool selected = false;
if(Find(8, cp, _, small)) for(int i = start;(i < (int)smallScales.size());i++) if(((((small.Height + small.Depth)) * smallScales[i]) > requiredHeight)) {
font = 8;
glyphScale = smallScales[i];
selected = true;
break;
}
if((!selected && !AlwaysStacks(cp))) for(int size = 1;(size <= 4);size++) if((Find((14 + size), cp, _, large) && (((((large.Height + large.Depth)) > requiredHeight) || ((size == 4) && NeverStacks(cp)))))) {
font = (14 + size);
selected = true;
break;
}
stack = !selected;
}
if(stack) return StackedDelimiter(cp, requiredHeight, (center ? scale : 1), atom, decorations);
if(!Find(font, cp, glyphIndex, metric)) throw CompileFailure{MathDiagnostic{MathDiagnosticCode::UnsupportedCommand,SourceRange{},u"The locked delimiter glyph is unavailable."}};
double paintShift = (center ? (((glyphScale - scale)) * MathConstants::axisHeight(MetricIndex(style))) : 0);
double logicalShift = ((glyphScale == 0) ? 0 : ((paintShift * scale) / glyphScale));
return ([&]{Box v{};v.Width=((FontData::Glyphs[glyphIndex].AdvanceUnits / (double)(FontData::Fonts[font].UnitsPerEm)) * glyphScale);v.Height=((metric.Height * glyphScale) - logicalShift);v.Depth=((metric.Depth * glyphScale) + logicalShift);v.Atom=atom;v.Symbol=true;v.Font=font;v.GlyphIndex=glyphIndex;v.GlyphScale=glyphScale;v.GlyphY=paintShift;return v;}());
}}
Box Layout::StackedDelimiter(int cp, double requiredHeight, double axisScale, MathAtom atom, std::vector<Decoration>& decorations){ int topGlyph{};TexGlyphMetric topMetric{};int bottomGlyph{};TexGlyphMetric bottomMetric{};TexGlyphMetric repeatMetric{};{
int font = 18, top{}, repeat{}, bottom{}, middle = 0;
std::optional<std::u16string> label = std::nullopt;
double pathWidth = 0;
switch(cp){
case u'(':
top = 0x239b;
repeat = 0x239c;
bottom = 0x239d;
label = u"lparen";
pathWidth = .875;
break;
case u')':
top = 0x239e;
repeat = 0x239f;
bottom = 0x23a0;
label = u"rparen";
pathWidth = .875;
break;
case u'[':
top = 0x23a1;
repeat = 0x23a2;
bottom = 0x23a3;
label = u"lbrack";
pathWidth = .667;
break;
case u']':
top = 0x23a4;
repeat = 0x23a5;
bottom = 0x23a6;
label = u"rbrack";
pathWidth = .667;
break;
case 0x230a:
top = repeat = 0x23a2;
bottom = 0x23a3;
label = u"lfloor";
pathWidth = .667;
break;
case 0x230b:
top = repeat = 0x23a5;
bottom = 0x23a6;
label = u"rfloor";
pathWidth = .667;
break;
case 0x2308:
top = 0x23a1;
repeat = bottom = 0x23a2;
label = u"lceil";
pathWidth = .667;
break;
case 0x2309:
top = 0x23a4;
repeat = bottom = 0x23a5;
label = u"rceil";
pathWidth = .667;
break;
case u'{':
top = 0x23a7;
middle = 0x23a8;
repeat = 0x23aa;
bottom = 0x23a9;
break;
case u'}':
top = 0x23ab;
middle = 0x23ac;
repeat = 0x23aa;
bottom = 0x23ad;
break;
case 0x27ee:
top = 0x23a7;
repeat = 0x23aa;
bottom = 0x23a9;
break;
case 0x27ef:
top = 0x23ab;
repeat = 0x23aa;
bottom = 0x23ad;
break;
case 0x23b0:
top = 0x23a7;
repeat = 0x23aa;
bottom = 0x23ad;
break;
case 0x23b1:
top = 0x23ab;
repeat = 0x23aa;
bottom = 0x23a9;
break;
case 0x2223:
font = 15;
top = bottom = cp;
repeat = 0x2223;
label = u"vert";
pathWidth = .333;
break;
case 0x2225:
font = 15;
top = bottom = cp;
repeat = 0x2225;
label = u"doublevert";
pathWidth = .556;
break;
case 0x2191:
font = 15;
top = cp;
repeat = bottom = 0x23d0;
break;
case 0x2193:
font = 15;
bottom = cp;
repeat = top = 0x23d0;
break;
case 0x2195:
font = 15;
top = 0x2191;
repeat = 0x23d0;
bottom = 0x2193;
break;
case 0x21d1:
font = 15;
top = cp;
repeat = bottom = 0x2016;
break;
case 0x21d3:
font = 15;
bottom = cp;
repeat = top = 0x2016;
break;
case 0x21d5:
font = 15;
top = 0x21d1;
repeat = 0x2016;
bottom = 0x21d3;
break;
default:
throw CompileFailure{MathDiagnostic{MathDiagnosticCode::UnsupportedCommand,SourceRange{},u"The locked stacked delimiter is not implemented."}};
}
if(((!Find(font, top, topGlyph, topMetric) || !Find(font, bottom, bottomGlyph, bottomMetric)) || !Find(font, repeat, _, repeatMetric))) throw CompileFailure{MathDiagnostic{MathDiagnosticCode::UnknownSymbol,SourceRange{},u"Missing delimiter assembly metric."}};
double th = (topMetric.Height + topMetric.Depth), bh = (bottomMetric.Height + bottomMetric.Depth);
double mh = 0;
int middleGlyph = -1;
TexGlyphMetric middleMetric = TexGlyphMetric{};
if(((middle != 0) && Find(font, middle, middleGlyph, middleMetric))) mh = (middleMetric.Height + middleMetric.Depth);
double rh = (repeatMetric.Height + repeatMetric.Depth);
double repeatCount = Math::Max(0,Math::Ceiling((((((requiredHeight - th) - bh) - mh)) / (((((middle == 0) ? 1 : 2)) * rh)))));
double realHeight = (((th + bh) + mh) + ((repeatCount * (((middle == 0) ? 1 : 2))) * rh));
double depth = ((realHeight / 2) - (.25 * axisScale)), height = (realHeight - depth);
double width = ((pathWidth > 0) ? pathWidth : (FontData::Glyphs[topGlyph].AdvanceUnits / (double)(FontData::Fonts[font].UnitsPerEm)));
Box box = ([&]{Box v{};v.Width=width;v.Height=height;v.Depth=depth;v.Atom=atom;return v;}());
if(!(!label)) {
double viewHeight = Math::Round((realHeight * 1000),MidpointRounding::AwayFromZero);
AddPath(box, MathPathCache::TallPath(*label, (int64_t)(Math::Round(((((realHeight - th) - bh)) * 1000),MidpointRounding::AwayFromZero))), 0, -height, .001, .001, MathBounds{0,-height,width,(viewHeight / 1000)}, decorations);
return box;
}
AddGlyph(box, font, bottomGlyph, bottom, 0, (depth - bottomMetric.Depth), 1, decorations);
AddGlyph(box, font, topGlyph, top, 0, (-height + topMetric.Height), 1, decorations);
double innerHeight = ((((((realHeight - th) - bh) - mh)) / (((middle == 0) ? 1 : 2))) + .016);
double lowerTop = (((depth - bh) + .008) - innerHeight);
AddPath(box, MathPathCache::InnerPath(repeat, (int64_t)(Math::Round((innerHeight * 1000),MidpointRounding::AwayFromZero))), 0, lowerTop, .001, .001, MathBounds{0,lowerTop,width,innerHeight}, decorations);
if((middle != 0)) {
double middleBaseline = ((lowerTop + .008) - middleMetric.Depth);
AddGlyph(box, font, middleGlyph, middle, 0, middleBaseline, 1, decorations);
double upperTop = (((middleBaseline - middleMetric.Height) + .008) - innerHeight);
AddPath(box, MathPathCache::InnerPath(repeat, (int64_t)(Math::Round((innerHeight * 1000),MidpointRounding::AwayFromZero))), 0, upperTop, .001, .001, MathBounds{0,upperTop,width,innerHeight}, decorations);
}
return box;
}}
void Layout::AttachDelimiter(Box& target, Box delimiter, double dx, std::vector<Decoration>& decorations){ {
target.Height = Math::Max(target.Height,delimiter.Height);
target.Depth = Math::Max(target.Depth,delimiter.Depth);
if(delimiter.Symbol) AddGlyph(target, delimiter.Font, delimiter.GlyphIndex, FontData::Glyphs[delimiter.GlyphIndex].CodePoint, dx, delimiter.GlyphY, delimiter.GlyphScale, decorations);
for(int current = delimiter.FirstDecoration;(current != 0);) {
Decoration decoration = decorations[(current - 1)];
current = decoration.Next;
decoration.X += dx;
decoration.Clip = ([&]{auto copy = decoration.Clip;copy.X=(decoration.Clip.X + dx);return copy;}());
CheckDecorationBudget(decorations);
decoration.Next = target.FirstDecoration;
decorations.push_back(decoration);
target.FirstDecoration = (int)decorations.size();
}
}}
void Layout::AddGlyph(Box& box, int font, int glyphIndex, int cp, double x, double y, double scale, std::vector<Decoration>& decorations){ {
CheckDecorationBudget(decorations);
decorations.push_back(([&]{Decoration v{};v.Kind=DecorationKind::Glyph;v.Next=box.FirstDecoration;v.Font=font;v.GlyphIndex=glyphIndex;v.CodePoint=cp;v.X=x;v.Y=y;v.ScaleX=scale;return v;}()));
box.FirstDecoration = (int)decorations.size();
}}
void Layout::AddPath(Box& box, const std::vector<MathPathSegment>& path, double x, double y, double scaleX, double scaleY, MathBounds clip, std::vector<Decoration>& decorations){ {
CheckDecorationBudget(decorations);
decorations.push_back(([&]{Decoration v{};v.Kind=DecorationKind::Path;v.Next=box.FirstDecoration;v.Path=path;v.X=x;v.Y=y;v.ScaleX=scaleX;v.ScaleY=scaleY;v.Clip=clip;return v;}()));
box.FirstDecoration = (int)decorations.size();
}}
void Layout::AddRule(Box& box, double x, double y, double width, double height, std::vector<Decoration>& decorations, bool vertical, bool exact, std::optional<MathColor> color){ {
CheckDecorationBudget(decorations);
decorations.push_back(([&]{Decoration v{};v.Kind=DecorationKind::Rule;v.Next=box.FirstDecoration;v.X=x;v.Y=y;v.Width=width;v.Height=height;v.Vertical=vertical;v.Exact=exact;v.HasColor=color.has_value();v.Color=color.value_or(MathColor{});return v;}()));
box.FirstDecoration = (int)decorations.size();
}}
void Layout::CheckDecorationBudget(std::vector<Decoration>& decorations){ {
if(((int)decorations.size() >= 262144)) throw CompileFailure{MathDiagnostic{MathDiagnosticCode::NodeLimit,SourceRange{},u"The bounded drawing-decoration budget was exceeded."}};
}}
Box Layout::BuildAccent(std::span<const SemanticNode> nodes, int id, LayoutContext style, const MathOptions& options, std::vector<Box>& boxes, std::vector<double>& x, std::vector<double>& y, std::vector<Decoration>& decorations){ TexGlyphMetric accent{};TexGlyphMetric fallbackAccent{};{
const SemanticNode& node = nodes[id];
Box body = ((node.FirstChild >= 0) ? boxes[node.FirstChild] : Box{});
double scale = Scale(style), theta = (MathConstants::defaultRuleThickness(MetricIndex(style)) * scale);
std::u16string label = value_or(node.Text, u"");
Box result = ([&]{Box v{};v.Width=body.Width;v.Height=body.Height;v.Depth=body.Depth;v.Atom=MathAtom::Ord;v.DecorationsAfter=true;return v;}());
if((node.FirstChild >= 0)) x[node.FirstChild] = y[node.FirstChild] = 0;
if(((label == u"\\overline") || (label == u"\\underline"))) {
bool under = (label == u"\\underline");
double line = Math::Max(theta,(options.MinRuleThickness * scale));
AddRule(result, 0, (under ? (body.Depth + (3 * theta)) : ((-body.Height - (3 * theta)) - line)), body.Width, line, decorations);
if(under) result.Depth += ((4 * theta) + line); else result.Height += ((4 * theta) + line);
return result;
}
if((starts_with(label, u"\\x") || starts_with(label, u"\\\\cd"))) {
Box lower = ((node.SecondChild >= 0) ? boxes[node.SecondChild] : Box{});
bool cd = starts_with(label, u"\\\\cd");
StretchyInfo info = GetStretchy(trim_start(label, u'\\'));
double arrowHeight = (info.Height * scale);
double labelPad = (cd ? .83334 : 1);
result.Width = Math::Max((info.MinWidth * scale),Math::Max(((node.FirstChild >= 0) ? (body.Width + (labelPad * Scale(style.WithStyle(SupStyles[style.Style])))) : 0),((node.SecondChild >= 0) ? (lower.Width + (labelPad * Scale(style.WithStyle(SubStyles[style.Style])))) : 0)));
double axis = (MathConstants::axisHeight(MetricIndex(style)) * scale);
double upperY = ((-axis - (.5 * arrowHeight)) - (.111 * scale));
if(((body.Depth > (.25 * scale)) || (label == u"\\xleftequilibrium"))) upperY -= body.Depth;
if((node.FirstChild >= 0)) {
x[node.FirstChild] = ((((result.Width - body.Width)) / 2) - ((cd ? (.13889 * Scale(style.WithStyle(SupStyles[style.Style]))) : 0)));
y[node.FirstChild] = upperY;
}
result.Height = Math::Max((axis + (arrowHeight / 2)),((node.FirstChild >= 0) ? (body.Height - upperY) : 0));
result.Depth = Math::Max(0,((arrowHeight / 2) - axis));
if((node.SecondChild >= 0)) {
x[node.SecondChild] = ((((result.Width - lower.Width)) / 2) - ((cd ? (.13889 * Scale(style.WithStyle(SubStyles[style.Style]))) : 0)));
y[node.SecondChild] = (((-axis + lower.Height) + (arrowHeight / 2)) + (.111 * scale));
result.Depth = Math::Max(result.Depth,(lower.Depth + y[node.SecondChild]));
}
AddStretchy(result, info, 0, (-axis - (arrowHeight / 2)), result.Width, scale, decorations);
result.Atom = MathAtom::Rel;
return result;
}
if(((((label == u"\\overbrace") || (label == u"\\underbrace")) || (label == u"\\overbracket")) || (label == u"\\underbracket"))) {
bool under = starts_with(label, u"\\under");
StretchyInfo info = GetStretchy(label.substr(1));
result.Width = Math::Max(body.Width,(info.MinWidth * scale));
x[node.FirstChild] = (((result.Width - body.Width)) / 2);
double gap = (.1 * scale), height = (info.Height * scale);
AddStretchy(result, info, 0, (under ? (body.Depth + gap) : ((-body.Height - gap) - height)), result.Width, scale, decorations);
if(under) result.Depth += (gap + height); else result.Height += (gap + height);
result.Atom = MathAtom::Inner;
return result;
}
double skew = CharacterSkew(nodes, node.FirstChild, boxes);
bool wide = (((label == u"\\widehat") || (label == u"\\widecheck")) || (label == u"\\widetilde"));
if((wide || ((((((((((((((label == u"\\overrightarrow") || (label == u"\\overleftarrow")) || (label == u"\\Overrightarrow")) || (label == u"\\overleftrightarrow")) || (label == u"\\overgroup")) || (label == u"\\overlinesegment")) || (label == u"\\overleftharpoon")) || (label == u"\\overrightharpoon")) || (label == u"\\underrightarrow")) || (label == u"\\underleftarrow")) || (label == u"\\underleftrightarrow")) || (label == u"\\undergroup")) || (label == u"\\underlinesegment")) || (label == u"\\utilde")))) {
bool under = (starts_with(label, u"\\under") || (label == u"\\utilde"));
if(!wide) skew = 0;
StretchyInfo info{};
if((wide || (label == u"\\utilde"))) {
int count = 1, inner = node.FirstChild;
if((nodes[inner].Kind == NodeKind::Row)) {
count = 0;
for(int child = nodes[inner].FirstChild;(child >= 0);child = nodes[child].NextSibling) count++;
}
int image = ((count > 5) ? 4 : ((count <= 1) ? 1 : ((count <= 3) ? 2 : 3)));
bool tilde = ((label == u"\\widetilde") || (label == u"\\utilde"));
double vw = (tilde ? ([&]() -> int { auto sw2 = image; if((sw2 == 1)) return 600; if((sw2 == 2)) return 1033; if((sw2 == 3)) return 2339; if(true) return 2340; throw std::logic_error("exhaustive switch"); }()) : ((image == 1) ? 1062 : 2364));
double vh = (tilde ? ([&]() -> int { auto sw3 = image; if((sw3 == 1)) return 260; if((sw3 == 2)) return 286; if((sw3 == 3)) return 306; if(true) return 312; throw std::logic_error("exhaustive switch"); }()) : ([&]() -> int { auto sw4 = image; if((sw4 == 1)) return 239; if((sw4 == 2)) return 300; if((sw4 == 3)) return 360; if(true) return 420; throw std::logic_error("exhaustive switch"); }()));
double h = (tilde ? ([&]() -> double { auto sw5 = image; if((sw5 == 1)) return .26; if((sw5 == 2)) return .286; if((sw5 == 3)) return .3; if(true) return .34; throw std::logic_error("exhaustive switch"); }()) : ([&]() -> double { auto sw6 = image; if((sw6 == 1)) return .24; if((sw6 == 2)) return .3; if((sw6 == 3)) return .3; if(true) return .42; throw std::logic_error("exhaustive switch"); }()));
std::u16string name = (tilde ? std::u16string(u"tilde") : label.substr(1)) + to_u16(image);
double underKern = ((label == u"\\utilde") ? (.12 * scale) : 0);
double width = Math::Max(0,(body.Width - (2 * skew))), top = (under ? (body.Depth + underKern) : (-body.Height - (h * scale)));
AddPath(result, MathPathCache::Get(name), (2 * skew), top, (width / vw), ((h * scale) / vh), MathBounds{(2 * skew),top,width,(h * scale)}, decorations);
if(under) result.Depth += ((h * scale) + underKern); else result.Height += (h * scale);
} else {
info = GetStretchy(label.substr(1));
result.Width = Math::Max(body.Width,(info.MinWidth * scale));
x[node.FirstChild] = (under ? 0 : (((result.Width - body.Width)) / 2));
AddStretchy(result, info, 0, (under ? body.Depth : (-body.Height - (info.Height * scale))), result.Width, scale, decorations);
if(under) result.Depth += (info.Height * scale); else result.Height += (info.Height * scale);
}
return result;
}
bool accentBelow = (label == u"\\c"), accentFull = (label == u"\\textcircled");
double clearance = (accentBelow ? (body.Height + body.Depth) : (accentFull ? body.Height : Math::Min(body.Height,(MathConstants::xHeight(MetricIndex(style)) * scale))));
double accentWidth{}, accentHeight{}, accentDepth = 0;
int accentFont = ([&]() -> int { auto sw7 = node.Font; if((sw7 == MathFont::MathBold)) return 5; if((sw7 == MathFont::MainBoldItalic)) return 6; if((sw7 == MathFont::MathItalic)) return 7; if((sw7 == MathFont::MathSans)) return 13; if((sw7 == MathFont::MathSansBold)) return 11; if((sw7 == MathFont::MathSansItalic)) return 12; if((sw7 == MathFont::MathTypewriter)) return 19; if(true) return 8; throw std::logic_error("exhaustive switch"); }()), accentGlyph = -1;
if((label == u"\\vec")) {
accentWidth = (.471 * scale);
accentHeight = (.714 * scale);
} else if(Find(accentFont, ((label == u"\\^") ? 0x02c6 : node.CodePoint), accentGlyph, accent)) {
accentWidth = ((FontData::Glyphs[accentGlyph].AdvanceUnits / (double)(FontData::Fonts[accentFont].UnitsPerEm)) * scale);
accentHeight = (accent.Height * scale);
accentDepth = (accent.Depth * scale);
} else if(((accentFont != 8) && Find(8, node.CodePoint, accentGlyph, fallbackAccent))) {
accentFont = 8;
accentWidth = ((FontData::Glyphs[accentGlyph].AdvanceUnits / (double)(FontData::Fonts[8].UnitsPerEm)) * scale);
accentHeight = (fallbackAccent.Height * scale);
accentDepth = (fallbackAccent.Depth * scale);
} else throw CompileFailure{MathDiagnostic{MathDiagnosticCode::UnsupportedCommand,node.Range,u"This accent requires an unavailable locked glyph."}};
if(accentBelow) clearance += accentDepth;
double ax = (((accentFull ? (((Math::Max(body.Width,accentWidth) - accentWidth)) / 2) : ((body.Width / 2) - (accentWidth / 2)))) + skew);
double ay = (((-body.Height + clearance) - accentDepth) + ((accentFull ? (.2 * scale) : 0)));
if((label == u"\\vec")) AddPath(result, MathPathCache::Get(u"vec"), ax, (ay - accentHeight), (.001 * scale), (.001 * scale), MathBounds{ax,(ay - accentHeight),accentWidth,accentHeight}, decorations); else AddGlyph(result, accentFont, accentGlyph, ((label == u"\\^") ? 0x02c6 : node.CodePoint), ax, ay, scale, decorations);
result.Height = Math::Max(result.Height,((accentHeight - ay) + ((accentFull ? (.2 * scale) : 0))));
result.Depth = Math::Max(result.Depth,((accentDepth + ay) - ((accentFull ? (.2 * scale) : 0))));
if(accentFull) {
result.Width = Math::Max(result.Width,accentWidth);
x[node.FirstChild] = (((result.Width - body.Width)) / 2);
}
return result;
}}
double Layout::CharacterSkew(std::span<const SemanticNode> nodes, int id, std::vector<Box>& boxes){ TexGlyphMetric metric{};{
while((id >= 0)) {
if(((((nodes[id].Kind == NodeKind::Font) || (nodes[id].Kind == NodeKind::Style)) || (nodes[id].Kind == NodeKind::Text)) || (((nodes[id].Kind == NodeKind::Row) && (nodes[id].FirstChild >= 0)) && (nodes[nodes[id].FirstChild].NextSibling < 0)))) {
id = nodes[id].FirstChild;
continue;
}
Box box = boxes[id];
if((box.Symbol && Find(box.Font, nodes[id].CodePoint, _, metric))) return (metric.Skew * (((box.GlyphScale == 0) ? 1 : box.GlyphScale)));
return 0;
}
return 0;
}}
StretchyInfo Layout::GetStretchy(std::u16string label) { return ([&]() -> StretchyInfo { auto sw8 = label; if((sw8 == u"cdrightarrow")) return StretchyInfo{u"rightarrow",std::nullopt,3,.522,true}; if((sw8 == u"cdleftarrow")) return StretchyInfo{u"leftarrow",std::nullopt,3,.522}; if((sw8 == u"cdlongequal")) return StretchyInfo{u"longequal",std::nullopt,3,.334}; if(((sw8 == u"overrightarrow") || (sw8 == u"underrightarrow"))) return StretchyInfo{u"rightarrow",std::nullopt,.888,.522,true}; if(((sw8 == u"overleftarrow") || (sw8 == u"underleftarrow"))) return StretchyInfo{u"leftarrow",std::nullopt,.888,.522}; if((sw8 == u"xrightarrow")) return StretchyInfo{u"rightarrow",std::nullopt,1.469,.522,true}; if((sw8 == u"xleftarrow")) return StretchyInfo{u"leftarrow",std::nullopt,1.469,.522}; if((sw8 == u"Overrightarrow")) return StretchyInfo{u"doublerightarrow",std::nullopt,.888,.56,true}; if((sw8 == u"xRightarrow")) return StretchyInfo{u"doublerightarrow",std::nullopt,1.526,.56,true}; if((sw8 == u"xLeftarrow")) return StretchyInfo{u"doubleleftarrow",std::nullopt,1.526,.56}; if(((sw8 == u"overleftharpoon") || (sw8 == u"xleftharpoonup"))) return StretchyInfo{u"leftharpoon",std::nullopt,.888,.522}; if(((sw8 == u"overrightharpoon") || (sw8 == u"xrightharpoonup"))) return StretchyInfo{u"rightharpoon",std::nullopt,.888,.522,true}; if((sw8 == u"xleftharpoondown")) return StretchyInfo{u"leftharpoondown",std::nullopt,.888,.522}; if((sw8 == u"xrightharpoondown")) return StretchyInfo{u"rightharpoondown",std::nullopt,.888,.522,true}; if((sw8 == u"xlongequal")) return StretchyInfo{u"longequal",std::nullopt,.888,.334}; if((sw8 == u"xtwoheadleftarrow")) return StretchyInfo{u"twoheadleftarrow",std::nullopt,.888,.334}; if((sw8 == u"xtwoheadrightarrow")) return StretchyInfo{u"twoheadrightarrow",std::nullopt,.888,.334,true}; if(((sw8 == u"overleftrightarrow") || (sw8 == u"underleftrightarrow"))) return StretchyInfo{u"leftarrow",u"rightarrow",.888,.522}; if((sw8 == u"xleftrightarrow")) return StretchyInfo{u"leftarrow",u"rightarrow",1.75,.522}; if((sw8 == u"xLeftrightarrow")) return StretchyInfo{u"doubleleftarrow",u"doublerightarrow",1.75,.56}; if((sw8 == u"xrightleftharpoons")) return StretchyInfo{u"leftharpoondownplus",u"rightharpoonplus",1.75,.716}; if((sw8 == u"xleftrightharpoons")) return StretchyInfo{u"leftharpoonplus",u"rightharpoondownplus",1.75,.716}; if((sw8 == u"xhookleftarrow")) return StretchyInfo{u"leftarrow",u"righthook",1.08,.522}; if((sw8 == u"xhookrightarrow")) return StretchyInfo{u"lefthook",u"rightarrow",1.08,.522}; if(((sw8 == u"overlinesegment") || (sw8 == u"underlinesegment"))) return StretchyInfo{u"leftlinesegment",u"rightlinesegment",.888,.522}; if((sw8 == u"overgroup")) return StretchyInfo{u"leftgroup",u"rightgroup",.888,.342}; if((sw8 == u"undergroup")) return StretchyInfo{u"leftgroupunder",u"rightgroupunder",.888,.342}; if((sw8 == u"xmapsto")) return StretchyInfo{u"leftmapsto",u"rightarrow",1.5,.522}; if((sw8 == u"xtofrom")) return StretchyInfo{u"leftToFrom",u"rightToFrom",1.75,.528}; if((sw8 == u"xrightleftarrows")) return StretchyInfo{u"baraboveleftarrow",u"rightarrowabovebar",1.75,.901}; if((sw8 == u"xrightequilibrium")) return StretchyInfo{u"baraboveshortleftharpoon",u"rightharpoonaboveshortbar",1.75,.716}; if((sw8 == u"xleftequilibrium")) return StretchyInfo{u"shortbaraboveleftharpoon",u"shortrightharpoonabovebar",1.75,.716}; if((sw8 == u"overbrace")) return StretchyInfo{u"leftbrace",u"rightbrace",1.6,.548,false,u"midbrace"}; if((sw8 == u"underbrace")) return StretchyInfo{u"leftbraceunder",u"rightbraceunder",1.6,.548,false,u"midbraceunder"}; if((sw8 == u"overbracket")) return StretchyInfo{u"leftbracketover",u"rightbracketover",1.6,.44}; if((sw8 == u"underbracket")) return StretchyInfo{u"leftbracketunder",u"rightbracketunder",1.6,.41}; if(true) throw CompileFailure{MathDiagnostic{MathDiagnosticCode::UnsupportedCommand,SourceRange{},u"This locked stretchy command is not implemented."}}; throw std::logic_error("exhaustive switch"); }()); }
void Layout::AddStretchy(Box& box, StretchyInfo info, double x, double y, double width, double scale, std::vector<Decoration>& decorations){ {
double height = (info.Height * scale), pathScale = Math::Max((width / 400000),(scale / 1000));
if(!info.Right) {
double dx = (info.AlignRight ? (width - (400000 * pathScale)) : 0);
AddPath(box, MathPathCache::Get(info.Left), (x + dx), y, pathScale, pathScale, MathBounds{x,y,width,height}, decorations);
return;
}
if(!info.Middle) {
double half = (width * .502);
AddPath(box, MathPathCache::Get(info.Left), x, y, pathScale, pathScale, MathBounds{x,y,half,height}, decorations);
AddPath(box, MathPathCache::Get(*info.Right), ((x + width) - (400000 * pathScale)), y, pathScale, pathScale, MathBounds{((x + width) - half),y,half,height}, decorations);
} else {
double quarter = (width * .251);
AddPath(box, MathPathCache::Get(info.Left), x, y, pathScale, pathScale, MathBounds{x,y,quarter,height}, decorations);
AddPath(box, MathPathCache::Get(*info.Middle), ((x + (width / 2)) - (200000 * pathScale)), y, pathScale, pathScale, MathBounds{(x + (width / 4)),y,(width / 2),height}, decorations);
AddPath(box, MathPathCache::Get(*info.Right), ((x + width) - (400000 * pathScale)), y, pathScale, pathScale, MathBounds{((x + width) - quarter),y,quarter,height}, decorations);
}
}}
Box Layout::BuildMatrix(std::span<const SemanticNode> nodes, const SemanticNode& node, LayoutContext style, const MathOptions& options, std::vector<Box>& boxes, std::vector<double>& x, std::vector<double>& y, std::vector<Decoration>& decorations){ int glyph{};{
std::u16string environment = value_or(node.Text, u"matrix");
bool aligned = (((((((environment == u"aligned") || (environment == u"alignedat")) || (environment == u"split")) || (environment == u"align")) || (environment == u"align*")) || (environment == u"alignat")) || (environment == u"alignat*")), gathered = (((environment == u"gathered") || (environment == u"gather")) || (environment == u"gather*"));
bool cd = (environment == u"CD"), addJot = (((aligned || gathered) || cd) || ((((((environment == u"align") || (environment == u"align*")) || (environment == u"alignat")) || (environment == u"alignat*")) || (environment == u"gather")) || (environment == u"gather*")));
bool small = ((environment == u"smallmatrix") || (environment == u"subarray")), cases = contains(environment, u"cases");
bool hskip = ((environment == u"array") || (environment == u"darray"));
double scale = Scale(style), stretch = (small ? .5 : (cases ? 1.2 : (cd ? 1 : ((node.Value2 > 0) ? node.Value2 : 1))));
double baselineSkip = (cd ? (3 * MathConstants::xHeight(style.Metrics)) : 1.2);
double strutHeight = (((.7 * baselineSkip) * stretch) * scale), strutDepth = (((.3 * baselineSkip) * stretch) * scale);
double fontSize = options.FontSize;
int columns = node.ColumnCount;
if((columns == 0)) for(int row = node.FirstChild;(row >= 0);row = nodes[row].NextSibling) {
int c = 0;
for(int cell = nodes[row].FirstChild;(cell >= 0);cell = nodes[cell].NextSibling) c++;
columns = Math::Max(columns,c);
}
auto widths = std::vector<double>(columns);
auto starts = std::vector<double>(columns);
auto alignments = std::vector<char16_t>(columns);
std::u16string spec = value_or(node.Alignment, u"");
int specColumn = 0;
for(auto align : spec) if((((align == u'c') || (align == u'l')) || (align == u'r'))) {
if((specColumn < columns)) alignments[specColumn++] = align;
}
for(int c = specColumn;(c < columns);c++) alignments[c] = (aligned ? ((((c % 2) == 0) ? u'r' : u'l')) : (cases ? u'l' : ((specColumn == 1) ? alignments[0] : u'c')));
if((aligned || cases)) for(int c = 0;(c < columns);c++) alignments[c] = (cases ? u'l' : (((c % 2) == 0) ? u'r' : u'l'));
double totalHeight = 0;
for(int row = node.FirstChild;(row >= 0);row = nodes[row].NextSibling) {
Box rowBox = ([&]{Box v{};v.Height=strutHeight;v.Depth=strutDepth;return v;}());
int c = 0;
for(int cell = nodes[row].FirstChild;(cell >= 0);cell = nodes[cell].NextSibling,c++) {
rowBox.Height = Math::Max(rowBox.Height,boxes[cell].Height);
rowBox.Depth = Math::Max(rowBox.Depth,boxes[cell].Depth);
if((c < columns)) widths[c] = Math::Max(widths[c],boxes[cell].Width);
}
double gap = ((nodes[row].Length1.Number != 0) ? Length(nodes[row].Length1, style, options) : (nodes[row].Value * scale));
if((gap > 0)) {
rowBox.Depth = Math::Max(rowBox.Depth,(gap + strutDepth));
gap = 0;
}
if((addJot && (nodes[row].NextSibling >= 0))) rowBox.Depth += (.3 * scale);
if((Math::Abs(nodes[row].RuleAbove) > 1)) totalHeight += ((((Math::Abs(nodes[row].RuleAbove) - 1)) * .25) * scale);
totalHeight += rowBox.Height;
y[row] = totalHeight;
totalHeight += (rowBox.Depth + gap);
boxes[row] = rowBox;
}
if((Math::Abs(node.RuleBelow) > 1)) totalHeight += ((((Math::Abs(node.RuleBelow) - 1)) * .25) * scale);
double offset = ((totalHeight / 2) + (MathConstants::axisHeight(MetricIndex(style)) * scale));
double separation = (small ? ((2 * .2778) * .7) : (cd ? (.5 * scale) : scale));
double width = 0;
auto separatorPositions = std::vector<std::tuple<double,bool>>{};
int specPosition = 0;
double ruleThickness = (.04 * scale);
double separatorAdvance = ((((1 + Math::Max(1,Math::Floor((ruleThickness * fontSize))))) / fontSize) - ruleThickness);
for(int c = 0;(c <= columns);c++) {
int separatorCount = 0;
while(((specPosition < (int)spec.size()) && ((spec[specPosition] == u'|') || (spec[specPosition] == u':')))) {
if((separatorCount++ > 0)) width += (.2 * scale);
separatorPositions.push_back(std::make_tuple(((width + (1 / fontSize)) - (ruleThickness / 2)),(spec[specPosition++] == u':')));
width += separatorAdvance;
}
if((c == columns)) break;
if(((specPosition < (int)spec.size()) && (((spec[specPosition] == u'c') || (spec[specPosition] == u'l')) || (spec[specPosition] == u'r')))) specPosition++;
if(hskip) width += (.5 * scale); else if((c > 0)) width += (aligned ? (((((c % 2) == 0) && (environment == u"aligned")) ? scale : 0)) : (gathered ? 0 : separation));
starts[c] = width;
width += widths[c];
if(hskip) width += (.5 * scale);
}
double logicalHeight = offset, logicalDepth = (totalHeight - offset);
if(cd) {
logicalHeight = logicalDepth = 0;
for(int row = node.FirstChild;(row >= 0);row = nodes[row].NextSibling) if((nodes[row].FirstChild >= 0)) {
logicalHeight = Math::Max(logicalHeight,(boxes[row].Height - ((y[row] - offset))));
logicalDepth = Math::Max(logicalDepth,((boxes[row].Depth + y[row]) - offset));
}
}
Box result = ([&]{Box v{};v.Width=width;v.Height=logicalHeight;v.Depth=logicalDepth;v.Atom=MathAtom::Ord;return v;}());
for(int row = node.FirstChild;(row >= 0);row = nodes[row].NextSibling) {
y[row] -= offset;
x[row] = 0;
int c = 0;
for(int cell = nodes[row].FirstChild;(cell >= 0);cell = nodes[cell].NextSibling,c++) {
x[cell] = (starts[c] + (((alignments[c] == u'r') ? (widths[c] - boxes[cell].Width) : ((alignments[c] == u'l') ? 0 : (((widths[c] - boxes[cell].Width)) / 2)))));
y[cell] = 0;
}
for(int line = 0;(line < Math::Abs(nodes[row].RuleAbove));line++) AddArrayHorizontal(result, ((y[row] - boxes[row].Height) - ((line * .25) * scale)), width, (.04 * scale), (nodes[row].RuleAbove < 0), fontSize, decorations);
}
for(int line = 0;(line < Math::Abs(node.RuleBelow));line++) AddArrayHorizontal(result, (result.Depth - ((line * .25) * scale)), width, (.04 * scale), (node.RuleBelow < 0), fontSize, decorations);
for(auto separator : separatorPositions) {
if(!std::get<1>(separator)) AddRule(result, std::get<0>(separator), -result.Height, (.04 * scale), totalHeight, decorations, true); else {
double stroke = (Math::Max(1,Math::Floor(((.04 * scale) * fontSize))) / fontSize);
double dash = (3 * stroke), count = Math::Max(1,Math::Round((((totalHeight + (2 * stroke))) / ((5 * stroke)))));
double gap = ((count <= 1) ? 0 : Math::Max(0,(((totalHeight - (count * dash))) / ((count - 1)))));
for(int i = 0;(i < (int)(count));i++) {
double top = (i * ((dash + gap)));
AddRule(result, std::get<0>(separator), (-result.Height + top), (.04 * scale), Math::Min(dash,(totalHeight - top)), decorations, true);
}
}
}
int equationNumber = options.EquationNumber;
for(int row = node.FirstChild;(row >= 0);row = nodes[row].NextSibling) {
if((nodes[row].SecondChild >= 0)) {
int tag = nodes[row].SecondChild;
x[tag] = (options.Leqno ? 0 : (Math::Max(width,(options.DisplayWidth / options.FontSize)) - boxes[tag].Width));
y[tag] = 0;
} else if((nodes[row].AlwaysLimits && !nodes[row].NeverLimits)) {
std::u16string label = ((u"(" + to_u16((equationNumber++))) + u")");
double labelWidth = 0;
for(auto c : label) if(Find(8, c, glyph, discard_ref<TexGlyphMetric>())) labelWidth += ((FontData::Glyphs[glyph].AdvanceUnits / (double)(FontData::Fonts[8].UnitsPerEm)) * scale);
double labelX = (options.Leqno ? 0 : (Math::Max(width,(options.DisplayWidth / options.FontSize)) - labelWidth));
Box rowBox = boxes[row];
for(auto c : label) if(Find(8, c, glyph, discard_ref<TexGlyphMetric>())) {
AddGlyph(rowBox, 8, glyph, c, labelX, 0, scale, decorations);
labelX += ((FontData::Glyphs[glyph].AdvanceUnits / (double)(FontData::Fonts[8].UnitsPerEm)) * scale);
}
boxes[row] = rowBox;
}
}
if(((node.LeftDelimiter != 0) || (node.RightDelimiter != 0))) {
double axis = (.25 * scale), distance = Math::Max((result.Height - axis),(result.Depth + axis));
double required = Math::Max(((distance * 901) / 500),((2 * distance) - .5));
Box left = MakeDelimiter(node.LeftDelimiter, required, style, true, 0, MathAtom::Open, decorations);
Box right = MakeDelimiter(node.RightDelimiter, required, style, true, 0, MathAtom::Close, decorations);
for(int row = node.FirstChild;(row >= 0);row = nodes[row].NextSibling) x[row] += left.Width;
for(int current = result.FirstDecoration;(current != 0);) {
Decoration decoration = decorations[(current - 1)];
int next = decoration.Next;
decoration.X += left.Width;
decoration.Clip = ([&]{auto copy = decoration.Clip;copy.X=(decoration.Clip.X + left.Width);return copy;}());
decorations[(current - 1)] = decoration;
current = next;
}
AttachDelimiter(result, left, 0, decorations);
AttachDelimiter(result, right, (left.Width + width), decorations);
result.Width += (left.Width + right.Width);
result.Atom = MathAtom::Inner;
}
return result;
}}
void Layout::AddArrayHorizontal(Box& result, double y, double width, double thickness, bool dashed, double fontSize, std::vector<Decoration>& decorations){ {
if(!dashed) AddRule(result, 0, (y - thickness), width, thickness, decorations); else {
double stroke = (Math::Max(1,Math::Floor((thickness * fontSize))) / fontSize);
double dash = (3 * stroke), count = Math::Max(1,Math::Round((((width + (2 * stroke))) / ((5 * stroke)))));
double gap = ((count <= 1) ? 0 : Math::Max(0,(((width - (count * dash))) / ((count - 1)))));
for(int i = 0;(i < (int)(count));i++) {
double left = (i * ((dash + gap)));
AddRule(result, left, (y - thickness), Math::Min(dash,(width - left)), thickness, decorations);
}
}
}}
double Layout::Scale(LayoutContext context) { return context.Scale; }
int Layout::MetricIndex(LayoutContext context) { return context.Metrics; }
double Layout::Length(TexLength value, LayoutContext context, const MathOptions& options){ {
double textScale = SizeMultipliers[(context.TextSize - 1)];
int textMetric = ((context.TextSize >= 5) ? 0 : ((context.TextSize >= 3) ? 1 : 2));
double em = ([&]() -> double { auto sw9 = value.Unit; if((sw9 == TexUnit::Em)) return (MathConstants::quad(textMetric) * textScale); if((sw9 == TexUnit::Ex)) return (MathConstants::xHeight(textMetric) * textScale); if((sw9 == TexUnit::Mu)) return ((MathConstants::quad(context.Metrics) * context.Scale) / 18); if((sw9 == TexUnit::Pt)) return .1; if(((sw9 == TexUnit::Bp) || (sw9 == TexUnit::Px))) return ((.1 * 803) / 800); if((sw9 == TexUnit::Pc)) return 1.2; if((sw9 == TexUnit::In)) return 7.227; if((sw9 == TexUnit::Cm)) return (722.7 / 254); if((sw9 == TexUnit::Mm)) return (722.7 / 2540); if((sw9 == TexUnit::Dd)) return ((.1 * 1238) / 1157); if((sw9 == TexUnit::Cc)) return ((.1 * 14856) / 1157); if((sw9 == TexUnit::Nd)) return ((.1 * 685) / 642); if((sw9 == TexUnit::Nc)) return ((.1 * 1370) / 107); if((sw9 == TexUnit::Sp)) return (.1 / 65536); if(true) throw std::logic_error("Unknown retained TeX length unit."); throw std::logic_error("exhaustive switch"); }());
return Math::Min((value.Number * em),(options.MaxSize * context.Scale));
}}
double Layout::MathMlLength(TexLength value, MathStyle style, uint8_t textSize, const MathOptions& options){ {
LayoutContext context = LayoutContext{(uint8_t)((((int)(style) * 2))),((textSize == 0) ? (uint8_t)(6) : textSize)};
return (Length(value, context, options) / context.Scale);
}}
double Layout::MathMlSizeScale(int size) { return SizeMultipliers[(size - 1)]; }
MathTransform Layout::Then(MathTransform local, MathTransform outer) { return MathTransform{((local.M11 * outer.M11) + (local.M12 * outer.M21)),((local.M11 * outer.M12) + (local.M12 * outer.M22)),((local.M21 * outer.M11) + (local.M22 * outer.M21)),((local.M21 * outer.M12) + (local.M22 * outer.M22)),(((local.Dx * outer.M11) + (local.Dy * outer.M21)) + outer.Dx),(((local.Dx * outer.M12) + (local.Dy * outer.M22)) + outer.Dy)}; }
LayoutOutput Layout::Build(std::span<const SemanticNode> nodes, int root, const MathOptions& options){ MathColor explicitColor{};{
int count = (int)nodes.size();
ScratchLease lease;
auto& scratch=lease.Get();
auto& boxes = Ensure(scratch.Boxes, count);
auto& styles = Ensure(scratch.Styles, count);
auto& scriptBases = Ensure(scratch.ScriptBases, count);
std::fill_n(scriptBases.begin(),count,uint8_t{});
for(int i = 0;(i < count);i++) if(((nodes[i].Kind == NodeKind::Script) && (nodes[i].FirstChild >= 0))) scriptBases[nodes[i].FirstChild] = true;
auto& x = Ensure(scratch.X, count);
auto& y = Ensure(scratch.Y, count);
auto& frames = Ensure(scratch.Frames, ((count * 2) + 4));
auto& sequence = Ensure(scratch.Sequence, count);
auto& collectStack = Ensure(scratch.CollectStack, count);
auto& atoms = Ensure(scratch.Atoms, count);
auto& colors = Ensure(scratch.Colors, count);
std::fill_n(colors.begin(),count,std::optional<MathColor>{});
auto& hidden = Ensure(scratch.Hidden, count);
std::fill_n(hidden.begin(),count,uint8_t{});
auto& colorStack = Ensure(scratch.ColorStack, count);
int colorTop = 0;
colorStack[colorTop++] = root;
while((colorTop != 0)) {
int colorNode = colorStack[--colorTop];
const SemanticNode& colored = nodes[colorNode];
auto ColorChild = [&](int child){
if((child >= 0)) {
colors[child] = colors[colorNode];
hidden[child] = hidden[colorNode];
colorStack[colorTop++] = child;
}
};
if((colored.Kind == NodeKind::Color)) colors[colorNode] = (try_parse_color(text(colored.Text2), explicitColor) ? explicitColor : colors[colorNode]);
if(((colored.Kind == NodeKind::Box) && (((colored.Text == u"\\phantom") || (colored.Text == u"\\vphantom")) || (colored.Text == u"\\hphantom")))) hidden[colorNode] = true;
if(((((((((((((colored.Kind == NodeKind::Row) || (colored.Kind == NodeKind::Style)) || (colored.Kind == NodeKind::Font)) || (colored.Kind == NodeKind::Text)) || (colored.Kind == NodeKind::Color)) || (colored.Kind == NodeKind::Sizing)) || (colored.Kind == NodeKind::Class)) || (colored.Kind == NodeKind::Operator)) || (colored.Kind == NodeKind::Matrix)) || (colored.Kind == NodeKind::MatrixRow)) || (colored.Kind == NodeKind::MatrixCell)) || ((colored.Kind == NodeKind::Box) && (((colored.Text == u"\\phantom") || (colored.Text == u"\\hbox")) || (colored.Text == u"\\pmb"))))) {
for(int child = colored.FirstChild;(child >= 0);child = nodes[child].NextSibling) ColorChild(child);
if((colored.Kind == NodeKind::MatrixRow)) ColorChild(colored.SecondChild);
} else {
ColorChild(colored.FirstChild);
ColorChild(colored.SecondChild);
ColorChild(colored.ThirdChild);
ColorChild(colored.FourthChild);
}

}
auto& decorations = scratch.Decorations;
decorations.clear();
int top = 0, batches = 0;
auto Push = [&](int child,LayoutContext childStyle){
if((child >= 0)) frames[top++] = Frame{child,childStyle,false};
};
frames[top++] = Frame{root,LayoutContext{(options.DisplayMode ? (uint8_t)(0) : (uint8_t)(2))},false};
while((top != 0)) {
if((((++batches & 63)) == 0)) check_cancel(options);
Frame frame = frames[--top];
int id = frame.Node;
const SemanticNode& node = nodes[id];
LayoutContext style = ((node.Kind == NodeKind::Style) ? frame.Context.WithStyle((uint8_t)((((int)(node.Style) * 2)))) : ((node.Kind == NodeKind::Sizing) ? frame.Context.WithSize((uint8_t)(node.Value)) : frame.Context));
styles[id] = style;
if(!frame.Complete) {
frames[top++] = Frame{id,style,true};
switch(node.Kind){
case NodeKind::Symbol:
case NodeKind::Space:
case NodeKind::Delimiter:
case NodeKind::Rule:
break;
case NodeKind::Choice:
Push(((style.Style < 2) ? node.FirstChild : ((style.Style < 4) ? node.SecondChild : ((style.Style < 6) ? node.ThirdChild : node.FourthChild))), style);
break;
case NodeKind::MathMlChoice:
Push(node.FirstChild, style);
break;
case NodeKind::Tag:
Push(node.FirstChild, style);
Push(node.SecondChild, style);
break;
case NodeKind::MatrixRow:
for(int child = node.FirstChild;(child >= 0);child = nodes[child].NextSibling) Push(child, style);
Push(node.SecondChild, style);
break;
case NodeKind::Script:
Push(node.FirstChild, style);
Push(node.SecondChild, style.WithStyle(SupStyles[style.Style]));
Push(node.ThirdChild, style.WithStyle(SubStyles[style.Style]));
break;
case NodeKind::Fraction:
Push(node.FirstChild, style.WithStyle(NumeratorStyles[style.Style]));
Push(node.SecondChild, style.WithStyle(DenominatorStyles[style.Style]));
break;
case NodeKind::Radical:
Push(node.FirstChild, style.WithStyle((uint8_t)(((style.Style | 1)))));
if((node.SecondChild >= 0)) Push(node.SecondChild, style.WithStyle(6));
break;
case NodeKind::Accent:
if((((node.Text && node.Text->starts_with(u"\\x")) == true) || ((node.Text && node.Text->starts_with(u"\\\\cd")) == true))) {
Push(node.FirstChild, style.WithStyle(SupStyles[style.Style]));
Push(node.SecondChild, style.WithStyle(SubStyles[style.Style]));
} else if(((((node.Text == u"\\overbrace") || (node.Text == u"\\underbrace")) || (node.Text == u"\\overbracket")) || (node.Text == u"\\underbracket"))) Push(node.FirstChild, style.WithStyle(0)); else Push(node.FirstChild, ((node.Text == u"\\underline") ? style : style.WithStyle((uint8_t)(((style.Style | 1))))));
break;
case NodeKind::Box:
for(int child = node.FirstChild;(child >= 0);child = nodes[child].NextSibling) Push(child, (((node.Text == u"cdleft") || (node.Text == u"cdright")) ? style.WithStyle(SupStyles[style.Style]) : style));
break;
default:
for(int child = node.FirstChild;(child >= 0);child = nodes[child].NextSibling) Push(child, style);
break;
}
continue;
}
double scale = Scale(style);
int metricIndex = MetricIndex(style);
switch(node.Kind){
case NodeKind::Symbol:
boxes[id] = Symbol(node, scale, options.TextShaper);
break;
case NodeKind::Operator:
{
std::optional<Box> operatorBody = ((((node.CodePoint == 0) && (node.FirstChild >= 0)) && (nodes[node.FirstChild].NextSibling >= 0)) ? std::optional<Box>(BuildRow(nodes, id, style, styles, boxes, x, y, sequence, collectStack, atoms, options)) : std::nullopt);
boxes[id] = BuildOperator(nodes, node, options.DisplayMode, style, boxes, decorations, operatorBody);
if((node.FirstChild >= 0)) {
if(!operatorBody) x[node.FirstChild] = 0;
for(int child = node.FirstChild;(child >= 0);child = nodes[child].NextSibling) y[child] = boxes[id].GlyphY;
}
break;
}
case NodeKind::Delimiter:
boxes[id] = MakeDelimiter(node.CodePoint, ((node.Value > 0) ? SizeHeight((int)(node.Value)) : 0), style, (node.Value == 0), (int)(node.Value), node.Atom, decorations);
break;
case NodeKind::Space:
boxes[id] = ([&]{Box v{};v.Width=((node.Text == u"\\\\") ? 0 : (((((((node.Text == u"\\kern") || (node.Text == u"\\mkern")) || (node.Text == u"\\hskip")) || (node.Text == u"\\mskip")) || (node.Text == u"\\hspace")) || (node.Text == u"\\hspace*")) ? Length(node.Length1, style, options) : ((node.Value * scale) * (((Math::Abs(node.Value) < 1) ? MathConstants::quad(metricIndex) : 1)))));return v;}());
break;
case NodeKind::Row:
case NodeKind::Style:
case NodeKind::Text:
case NodeKind::MatrixCell:
case NodeKind::MatrixRow:
case NodeKind::Color:
case NodeKind::Sizing:
{
boxes[id] = BuildRow(nodes, id, style, styles, boxes, x, y, sequence, collectStack, atoms, options);
if(((node.Kind == NodeKind::Row) && node.TextMode)) TryShapeTextRow(nodes, id, style, options, colors, boxes, x);
break;
}
case NodeKind::Class:
{
Box child = BuildRow(nodes, id, style, styles, boxes, x, y, sequence, collectStack, atoms, options);
child.Atom = node.Atom;
child.FirstDecoration = 0;
x[node.FirstChild] = y[node.FirstChild] = 0;
boxes[id] = child;
break;
}
case NodeKind::Choice:
case NodeKind::MathMlChoice:
{
int selected = ((node.Kind == NodeKind::MathMlChoice) ? node.FirstChild : ((style.Style < 2) ? node.FirstChild : ((style.Style < 4) ? node.SecondChild : ((style.Style < 6) ? node.ThirdChild : node.FourthChild))));
Box child = boxes[selected];
child.FirstDecoration = 0;
x[selected] = y[selected] = 0;
boxes[id] = child;
break;
}
case NodeKind::Box:
boxes[id] = BuildBox(nodes, node, style, options, boxes, x, y, ((((node.Text == u"\\phantom") || (node.Text == u"\\hbox")) || (node.Text == u"\\pmb")) ? std::optional<Box>(BuildRow(nodes, id, style, styles, boxes, x, y, sequence, collectStack, atoms, options)) : std::nullopt));
break;
case NodeKind::Rule:
{
double width = Length(node.Length1, style, options), height = Length(node.Length2, style, options), shift = Length(node.Length3, style, options);
Box result = ([&]{Box v{};v.Width=width;v.Height=(height + shift);v.Depth=-shift;v.Atom=MathAtom::Ord;return v;}());
if(((width > 0) && (height > 0))) {
double paintedHeight = (((1 + Math::Max(1,Math::Floor((height * options.FontSize))))) / options.FontSize);
AddRule(result,0,(-paintedHeight - shift),width,paintedHeight,decorations,false,true,std::nullopt);
}
boxes[id] = result;
break;
}
case NodeKind::Enclose:
boxes[id] = BuildEnclose(nodes, node, style, options, boxes, x, y, decorations);
break;
case NodeKind::Verb:
boxes[id] = BuildVerb(node, style, decorations);
break;
case NodeKind::Tag:
{
Box body = boxes[node.FirstChild], tag = ((node.SecondChild >= 0) ? boxes[node.SecondChild] : Box{});
x[node.FirstChild] = y[node.FirstChild] = 0;
if((node.SecondChild >= 0)) {
x[node.SecondChild] = (options.Leqno ? 0 : (Math::Max(body.Width,(options.DisplayWidth / options.FontSize)) - tag.Width));
y[node.SecondChild] = 0;
}
body.FirstDecoration = 0;
boxes[id] = body;
break;
}
case NodeKind::Font:
{
int basis = node.FirstChild;
if((((nodes[basis].Kind == NodeKind::Row) && (nodes[basis].FirstChild >= 0)) && (nodes[nodes[basis].FirstChild].NextSibling < 0))) basis = nodes[basis].FirstChild;
Box box = boxes[basis];
box.FirstDecoration = 0;
x[node.FirstChild] = y[node.FirstChild] = 0;
boxes[id] = box;
break;
}
case NodeKind::Script:
{
Box basis = ((node.FirstChild >= 0) ? boxes[node.FirstChild] : ([&]{Box v{};v.Atom=MathAtom::Ord;return v;}()));
Box scriptBasis = basis;
if((((node.FirstChild >= 0) && (nodes[node.FirstChild].Kind == NodeKind::Accent)) && (((((((((((nodes[node.FirstChild].Text == u"\\acute") || (nodes[node.FirstChild].Text == u"\\grave")) || (nodes[node.FirstChild].Text == u"\\ddot")) || (nodes[node.FirstChild].Text == u"\\tilde")) || (nodes[node.FirstChild].Text == u"\\bar")) || (nodes[node.FirstChild].Text == u"\\breve")) || (nodes[node.FirstChild].Text == u"\\check")) || (nodes[node.FirstChild].Text == u"\\hat")) || (nodes[node.FirstChild].Text == u"\\vec")) || (nodes[node.FirstChild].Text == u"\\dot")) || (nodes[node.FirstChild].Text == u"\\mathring")))) {
int inner = nodes[node.FirstChild].FirstChild;
if(((inner >= 0) && boxes[inner].Character)) scriptBasis = boxes[inner];
}
Box sup = ((node.SecondChild >= 0) ? boxes[node.SecondChild] : Box{});
Box sub = ((node.ThirdChild >= 0) ? boxes[node.ThirdChild] : Box{});
bool hasSup = (node.SecondChild >= 0), hasSub = (node.ThirdChild >= 0);
if((((node.FirstChild >= 0) && (nodes[node.FirstChild].Kind == NodeKind::Operator)) && UseLimits(nodes[node.FirstChild], style))) {
boxes[id] = BuildLimits(node, basis, sup, sub, style, boxes, x, y);
break;
}
if((((node.FirstChild >= 0) && (nodes[node.FirstChild].Kind == NodeKind::Accent)) && (((((nodes[node.FirstChild].Text == u"\\overbrace") || (nodes[node.FirstChild].Text == u"\\overbracket")) && !hasSub) || (((nodes[node.FirstChild].Text == u"\\underbrace") || (nodes[node.FirstChild].Text == u"\\underbracket")) && !hasSup))))) {
double width = Math::Max(basis.Width,(hasSup ? sup.Width : sub.Width));
x[node.FirstChild] = (((width - basis.Width)) / 2);
y[node.FirstChild] = 0;
Box braceResult = ([&]{Box v{};v.Width=width;v.Height=basis.Height;v.Depth=basis.Depth;v.Atom=MathAtom::Inner;return v;}());
if(hasSup) {
x[node.SecondChild] = (((width - sup.Width)) / 2);
y[node.SecondChild] = ((-basis.Height - (.2 * scale)) - sup.Depth);
braceResult.Height = (sup.Height - y[node.SecondChild]);
}
if(hasSub) {
x[node.ThirdChild] = (((width - sub.Width)) / 2);
y[node.ThirdChild] = ((basis.Depth + (.2 * scale)) + sub.Height);
braceResult.Depth = (sub.Depth + y[node.ThirdChild]);
}
boxes[id] = braceResult;
break;
}
double supShift = 0, subShift = 0;
if(!scriptBasis.Character) {
LayoutContext supStyle = style.WithStyle(SupStyles[style.Style]), subStyle = style.WithStyle(SubStyles[style.Style]);
if(hasSup) supShift = (scriptBasis.Height - (MathConstants::supDrop(MetricIndex(supStyle)) * Scale(supStyle)));
if(hasSub) subShift = (scriptBasis.Depth + (MathConstants::subDrop(MetricIndex(subStyle)) * Scale(subStyle)));
}
double minSupShift = ((((style.Style == 0) ? MathConstants::sup1(metricIndex) : ((((style & 1)) != 0) ? MathConstants::sup3(metricIndex) : MathConstants::sup2(metricIndex)))) * scale);
double xHeight = (MathConstants::xHeight(metricIndex) * scale);
if((hasSup && hasSub)) {
supShift = Math::Max(Math::Max(supShift,minSupShift),(sup.Depth + (0.25 * xHeight)));
subShift = Math::Max(subShift,(MathConstants::sub2(metricIndex) * scale));
double minimumGap = ((4 * MathConstants::defaultRuleThickness(metricIndex)) * scale);
if(((((supShift - sup.Depth)) - ((sub.Height - subShift))) < minimumGap)) {
subShift = ((minimumGap - ((supShift - sup.Depth))) + sub.Height);
double psi = ((0.8 * xHeight) - ((supShift - sup.Depth)));
if((psi > 0)) {
supShift += psi;
subShift -= psi;
}
}
} else if(hasSub) subShift = Math::Max(Math::Max(subShift,(MathConstants::sub1(metricIndex) * scale)),(sub.Height - (0.8 * xHeight))); else if(hasSup) supShift = Math::Max(Math::Max(supShift,minSupShift),(sup.Depth + (0.25 * xHeight)));
double correction = (scriptBasis.Symbol ? scriptBasis.Italic : 0);
double scriptWidth = 0;
Box result = ([&]{Box v{};v.Width=basis.Width;v.Height=Math::Max(0,basis.Height);v.Depth=Math::Max(0,basis.Depth);v.Atom=basis.Atom;return v;}());
if((node.FirstChild >= 0)) x[node.FirstChild] = y[node.FirstChild] = 0;
if(hasSup) {
x[node.SecondChild] = basis.Width;
y[node.SecondChild] = -supShift;
scriptWidth = (sup.Width + 0.05);
result.Height = Math::Max(result.Height,(sup.Height + supShift));
result.Depth = Math::Max(result.Depth,(sup.Depth - supShift));
}
if(hasSub) {
x[node.ThirdChild] = (basis.Width - correction);
y[node.ThirdChild] = subShift;
scriptWidth = Math::Max(scriptWidth,((sub.Width - correction) + 0.05));
result.Height = Math::Max(result.Height,(sub.Height - subShift));
result.Depth = Math::Max(result.Depth,(sub.Depth + subShift));
}
result.Width += Math::Max(0,scriptWidth);
boxes[id] = result;
break;
}
case NodeKind::Fraction:
{
Box numerator = boxes[node.FirstChild], denominator = boxes[node.SecondChild];
if((node.Text == u"\\cfrac")) {
numerator.Height = Math::Max(numerator.Height,(.85 * scale));
numerator.Depth = Math::Max(numerator.Depth,(.35 * scale));
}
double defaultRule = (MathConstants::defaultRuleThickness(metricIndex) * scale);
double specifiedRule = ((node.Text == u"\\genfrac") ? Length(node.Length1, style, options) : (node.Value * scale));
double rule = (node.HasBar ? Math::Max(((node.Value > 0) ? specifiedRule : defaultRule),(options.MinRuleThickness * scale)) : 0);
double numShift = ((((style < 2) ? MathConstants::num1(metricIndex) : (node.HasBar ? MathConstants::num2(metricIndex) : MathConstants::num3(metricIndex)))) * scale);
double denShift = ((((style < 2) ? MathConstants::denom1(metricIndex) : MathConstants::denom2(metricIndex))) * scale);
double clearance = ((((style < 2) ? (node.HasBar ? 3 : 7) : (node.HasBar ? 1 : 3))) * ((node.HasBar ? rule : defaultRule)));
double axis = (MathConstants::axisHeight(metricIndex) * scale);
if(node.HasBar) {
numShift += Math::Max(0,(clearance - ((((numShift - numerator.Depth)) - ((axis + (0.5 * rule)))))));
denShift += Math::Max(0,(clearance - ((((axis - (0.5 * rule))) - ((denominator.Height - denShift))))));
} else {
double missing = (Math::Max(0,(clearance - ((((numShift - numerator.Depth)) - ((denominator.Height - denShift)))))) / 2);
numShift += missing;
denShift += missing;
}
double contentWidth = Math::Max(numerator.Width,denominator.Width);
double delimiterSize = ((style < 2) ? MathConstants::delim1(metricIndex) : MathConstants::delim2(((style >= 6) ? 1 : metricIndex)));
Box left = MakeDelimiter(node.LeftDelimiter, delimiterSize, style, true, 0, MathAtom::Open, decorations);
Box right = ((node.Text == u"\\cfrac") ? Box{} : MakeDelimiter(node.RightDelimiter, delimiterSize, style, true, 0, MathAtom::Close, decorations));
x[node.FirstChild] = (left.Width + (((contentWidth - numerator.Width)) / 2));
y[node.FirstChild] = -numShift;
x[node.SecondChild] = (left.Width + (((contentWidth - denominator.Width)) / 2));
y[node.SecondChild] = denShift;
Box fraction = ([&]{Box v{};v.Width=((contentWidth + left.Width) + right.Width);v.Height=Math::Max((numerator.Height + numShift),(axis + (0.5 * rule)));v.Depth=Math::Max((denominator.Depth + denShift),(-axis + (0.5 * rule)));v.Atom=MathAtom::Ord;v.PathX=left.Width;v.PathY=(-axis - (0.5 * rule));v.PathScale=contentWidth;v.PathHeight=rule;return v;}());
AttachDelimiter(fraction, left, 0, decorations);
AttachDelimiter(fraction, right, (left.Width + contentWidth), decorations);
boxes[id] = fraction;
break;
}
case NodeKind::Delimited:
{
Box body = boxes[node.FirstChild];
double axis = (MathConstants::axisHeight(metricIndex) * scale);
double distance = Math::Max((body.Height - axis),(body.Depth + axis));
double height = Math::Max(((distance * 901) / 500),((2 * distance) - .5));
int length = Collect(nodes, nodes[node.FirstChild].FirstChild, sequence, collectStack, styles);
for(int i = 0;(i < length);i++) {
int middle = sequence[i];
if(((nodes[middle].Kind == NodeKind::Delimiter) && (nodes[middle].Value == 0))) boxes[middle] = MakeDelimiter(nodes[middle].CodePoint, height, styles[middle], true, 0, MathAtom::Ord, decorations);
}
body = boxes[node.FirstChild] = BuildRow(nodes, node.FirstChild, styles[node.FirstChild], styles, boxes, x, y, sequence, collectStack, atoms, options);
Box left = MakeDelimiter(node.LeftDelimiter, height, style, true, 0, MathAtom::Open, decorations);
Box right = MakeDelimiter(node.RightDelimiter, height, style, true, 0, MathAtom::Close, decorations);
x[node.FirstChild] = left.Width;
y[node.FirstChild] = 0;
Box result = ([&]{Box v{};v.Width=((left.Width + body.Width) + right.Width);v.Height=body.Height;v.Depth=body.Depth;v.Atom=MathAtom::Inner;return v;}());
AttachDelimiter(result, left, 0, decorations);
AttachDelimiter(result, right, (left.Width + body.Width), decorations);
boxes[id] = result;
break;
}
case NodeKind::Accent:
boxes[id] = BuildAccent(nodes, id, style, options, boxes, x, y, decorations);
break;
case NodeKind::Matrix:
boxes[id] = BuildMatrix(nodes, node, style, options, boxes, x, y, decorations);
break;
case NodeKind::Radical:
{
Box inner = boxes[node.FirstChild];
double innerHeight = ((inner.Height == 0) ? (MathConstants::xHeight(metricIndex) * scale) : inner.Height);
double theta = (MathConstants::defaultRuleThickness(metricIndex) * scale);
double phi = ((style < 2) ? (MathConstants::xHeight(metricIndex) * scale) : theta);
double clearance = (theta + (phi / 4));
double minimumHeight = (((innerHeight + inner.Depth) + clearance) + theta);
double localHeight = (minimumHeight / scale);
double baseFactor = (scale / Scale(style.Style));
int size = SelectRadical((minimumHeight / baseFactor));
double multiplier = Scale(style.Style);
double extraVinculum = Math::Max(0,(options.MinRuleThickness - MathConstants::sqrtRuleThickness(metricIndex)));
double texHeight{}, spanHeight{}, viewBoxHeight{}, advance{}, minWidth{};
std::vector<MathPathSegment> path{};
if((size == 0)) {
if((localHeight < 1)) multiplier = 1; else if((localHeight < 1.4)) multiplier = 0.7;
texHeight = ((((1 + extraVinculum)) * scale) / multiplier);
spanHeight = ((((1.08 + extraVinculum)) * scale) / multiplier);
viewBoxHeight = (1080 + (1000 * extraVinculum));
advance = ((0.833 * scale) / multiplier);
minWidth = (0.853 * scale);
path = RadicalPathData::sqrtMain;
} else if((size < 5)) {
double maxHeight = ([&]() -> double { auto sw10 = size; if((sw10 == 1)) return 1.2; if((sw10 == 2)) return 1.8; if((sw10 == 3)) return 2.4; if(true) return 3; throw std::logic_error("exhaustive switch"); }());
texHeight = (((maxHeight + extraVinculum)) * baseFactor);
spanHeight = ((((maxHeight + extraVinculum) + 0.08)) * baseFactor);
viewBoxHeight = (1080 * maxHeight);
advance = baseFactor;
minWidth = (1.02 * scale);
path = ([&]() -> std::vector<MathPathSegment> { auto sw11 = size; if((sw11 == 1)) return RadicalPathData::sqrtSize1; if((sw11 == 2)) return RadicalPathData::sqrtSize2; if((sw11 == 3)) return RadicalPathData::sqrtSize3; if(true) return RadicalPathData::sqrtSize4; throw std::logic_error("exhaustive switch"); }());
} else {
texHeight = (minimumHeight + (extraVinculum * scale));
spanHeight = (minimumHeight + (((extraVinculum + 0.08)) * scale));
viewBoxHeight = (Math::Floor(((1000 * localHeight) + extraVinculum)) + 80);
advance = (1.056 * scale);
minWidth = (0.742 * scale);
path = MathPathCache::Radical(viewBoxHeight);
}
if((extraVinculum > 0)) path = MathPathCache::RadicalWithRule(size, extraVinculum, viewBoxHeight);
double rule = ((((MathConstants::sqrtRuleThickness(metricIndex) + extraVinculum)) * multiplier) * scale);
double delimiterDepth = (texHeight - rule);
if((delimiterDepth > ((innerHeight + inner.Depth) + clearance))) clearance = (((((clearance + delimiterDepth) - innerHeight) - inner.Depth)) / 2);
double imageShift = (((texHeight - innerHeight) - clearance) - rule);
double width = Math::Max((inner.Width + advance),minWidth);
x[node.FirstChild] = advance;
y[node.FirstChild] = 0;
double pathScale = Math::Max((width / 400000),(spanHeight / viewBoxHeight));
Box result = ([&]{Box v{};v.Width=width;v.Height=((innerHeight + clearance) + (2 * rule));v.Depth=Math::Max(inner.Depth,imageShift);v.Atom=MathAtom::Ord;v.Path=path;v.PathX=0;v.PathY=(imageShift - spanHeight);v.PathScale=pathScale;v.PathHeight=spanHeight;return v;}());
if((node.SecondChild >= 0)) {
Box index = boxes[node.SecondChild];
double shift = (0.6 * ((result.Height - result.Depth)));
double rootWidth = (index.Width - ((5 * scale) / 18));
double left = ((5 * scale) / 18);
x[node.SecondChild] = left;
y[node.SecondChild] = -shift;
x[node.FirstChild] += rootWidth;
result.PathX += rootWidth;
result.Width += rootWidth;
result.Height = Math::Max(result.Height,(index.Height + shift));
result.Depth = Math::Max(result.Depth,(index.Depth - shift));
}
boxes[id] = result;
break;
}
default:
throw std::logic_error("Unknown compact semantic node kind.");
}
}
check_cancel(options);
double fontSize = options.FontSize;
Box outer = boxes[root];
double rootOffset = 0;
if((options.DisplayMode && (options.DisplayWidth > 0))) {
double available = (options.DisplayWidth / fontSize);
rootOffset = (options.Fleqn ? 2 : Math::Max(0,(((available - outer.Width)) / 2)));
outer.Width = Math::Max(available,(outer.Width + rootOffset));
for(int i = 0;(i < count);i++) if((((nodes[i].Kind == NodeKind::Tag) || (nodes[i].Kind == NodeKind::MatrixRow)) && (nodes[i].SecondChild >= 0))) x[nodes[i].SecondChild] -= rootOffset;
}
auto& glyphs = scratch.Glyphs;
glyphs.clear();
auto& rules = scratch.Rules;
rules.clear();
auto& paths = scratch.Paths;
paths.clear();
auto& textGlyphs = scratch.TextGlyphs;
textGlyphs.clear();
auto& textRuns = scratch.TextRuns;
textRuns.clear();
auto& paintCommands = scratch.PaintCommands;
paintCommands.clear();
auto& paintStates = scratch.PaintStates;
paintStates.clear();
paintStates.push_back(MathPaintState{std::nullopt,MathTransform::Identity()});



auto& placements = Ensure(scratch.Placements, ((count * 3) + 4));
int placementCount = 0;
int activePaintState = 0;
bool activeVisible = true;
placements[placementCount++] = Placement{root,rootOffset,outer.Height};
double inkLeft = INFINITY, inkTop = INFINITY, inkRight = -INFINITY, inkBottom = -INFINITY;
auto AddInk = [&](double left,double upper,double right,double bottom){
if(!activeVisible) return;
if(((right <= left) || (bottom <= upper))) return;
MathTransform transform = paintStates[activePaintState].Transform;
double x1 = (((left * transform.M11) + (upper * transform.M21)) + transform.Dx), y1 = (((left * transform.M12) + (upper * transform.M22)) + transform.Dy);
double x2 = (((right * transform.M11) + (upper * transform.M21)) + transform.Dx), y2 = (((right * transform.M12) + (upper * transform.M22)) + transform.Dy);
double x3 = (((left * transform.M11) + (bottom * transform.M21)) + transform.Dx), y3 = (((left * transform.M12) + (bottom * transform.M22)) + transform.Dy);
double x4 = (((right * transform.M11) + (bottom * transform.M21)) + transform.Dx), y4 = (((right * transform.M12) + (bottom * transform.M22)) + transform.Dy);
left = Math::Min(Math::Min(x1,x2),Math::Min(x3,x4));
right = Math::Max(Math::Max(x1,x2),Math::Max(x3,x4));
upper = Math::Min(Math::Min(y1,y2),Math::Min(y3,y4));
bottom = Math::Max(Math::Max(y1,y2),Math::Max(y3,y4));
inkLeft = Math::Min(inkLeft,left);
inkTop = Math::Min(inkTop,upper);
inkRight = Math::Max(inkRight,right);
inkBottom = Math::Max(inkBottom,bottom);
};
auto EmitGlyph = [&](int font,int glyphIndex,int codePoint,double gx,double gy,double emSize){
FontGlyph glyph = FontData::Glyphs[glyphIndex];
double size = (emSize * fontSize);
glyphs.push_back(MathGlyph{font,uint16_t(glyph.GlyphId),codePoint,(gx * fontSize),(gy * fontSize),size});
if(activeVisible) paintCommands.push_back(MathPaintCommand{MathPaintKind::Glyph,((int)glyphs.size() - 1),activePaintState});
GlyphInk ink = GlyphInkData::Bounds[glyphIndex];
double units = (size / FontData::Fonts[font].UnitsPerEm);
if(((ink.Left != ink.Right) && (ink.Top != ink.Bottom))) AddInk(((gx * fontSize) + (ink.Left * units)), ((gy * fontSize) - (ink.Top * units)), ((gx * fontSize) + (ink.Right * units)), ((gy * fontSize) - (ink.Bottom * units)));
};
auto GetState = [&](MathPaintState state) -> int{
for(int existing=0;existing<(int)paintStates.size();++existing)if(paintStates[existing]==state)return existing;
int id = (int)paintStates.size();
paintStates.push_back(state);

return id;
};
while((placementCount != 0)) {
if((((++batches & 63)) == 0)) check_cancel(options);
Placement placement = placements[--placementCount];
if(((((int64_t)((int)glyphs.size()) + (int)rules.size()) + (int)paths.size()) >= 262144)) throw CompileFailure{MathDiagnostic{MathDiagnosticCode::NodeLimit,SourceRange{},u"The bounded drawing-instruction budget was exceeded."}};
int id = placement.Node;
const SemanticNode& node = nodes[id];
const Box& box = boxes[id];
double px = placement.X, py = placement.Y;
MathPaintState inherited = paintStates[placement.State];
MathPaintState currentState = MathPaintState{value_or(colors[id], inherited.Color),inherited.Transform};
int stateId = GetState(currentState);
auto AddPaint = [&](MathPaintKind kind,int index,std::optional<int> explicitState = std::nullopt){
if((!hidden[id] && (optional_member(paintStates[value_or(explicitState, stateId)].Color, &MathColor::A) != 0))) paintCommands.push_back(MathPaintCommand{kind,index,value_or(explicitState, stateId)});
};
auto Place = [&](int child){ placements[placementCount++] = Placement{child,(px + x[child]),(py + y[child]),stateId}; };
bool visible = (!hidden[id] && (optional_member(currentState.Color, &MathColor::A) != 0));
activePaintState = stateId;
activeVisible = visible;
if((!placement.Decorations && box.DecorationsAfter)) placements[placementCount++] = ([&]{auto copy = placement;copy.Decorations=true;copy.State=stateId;return copy;}());
for(int decorationId = ((placement.Decorations || !box.DecorationsAfter) ? box.FirstDecoration : 0);(decorationId != 0);) {
Decoration decoration = decorations[(decorationId - 1)];
decorationId = decoration.Next;
int decorationState = (decoration.HasColor ? GetState(([&]{auto copy = currentState;copy.Color=decoration.Color;return copy;}())) : stateId);
activePaintState = decorationState;
activeVisible = (!hidden[id] && (optional_member(paintStates[decorationState].Color, &MathColor::A) != 0));
if((decoration.Kind == DecorationKind::Glyph)) EmitGlyph(decoration.Font, decoration.GlyphIndex, decoration.CodePoint, (px + decoration.X), (py + decoration.Y), decoration.ScaleX); else if((decoration.Kind == DecorationKind::Rule)) {
double left = (((px + decoration.X)) * fontSize), upper = (((py + decoration.Y)) * fontSize);
double width = (decoration.Width * fontSize), height = (decoration.Height * fontSize);
if(!decoration.Exact) {
if(decoration.Vertical) {
double exactWidth = width;
width = Math::Max(1,Math::Floor(width));
left += (exactWidth - width);
} else {
double exactHeight = height;
height = Math::Max(1,Math::Floor(height));
upper += (exactHeight - height);
}
}
rules.push_back(MathRule{left,upper,width,height});
AddPaint(MathPaintKind::Rule, ((int)rules.size() - 1), decorationState);
AddInk(left, upper, (left + width), (upper + height));
} else {
double dx = (((px + decoration.X)) * fontSize), dy = (((py + decoration.Y)) * fontSize);
double sx = (decoration.ScaleX * fontSize), sy = (decoration.ScaleY * fontSize);
MathBounds clip = MathBounds{(((px + decoration.Clip.X)) * fontSize),(((py + decoration.Clip.Y)) * fontSize),(decoration.Clip.Width * fontSize),(decoration.Clip.Height * fontSize)};
paths.push_back(MathPath{decoration.Path,dx,dy,sx,sy,clip});
AddPaint(MathPaintKind::Path, ((int)paths.size() - 1), decorationState);
MathBounds pathBounds = PathBounds(decoration.Path);
AddInk(Math::Max(clip.X,(dx + (pathBounds.X * sx))), Math::Max(clip.Y,(dy + (pathBounds.Y * sy))), Math::Min((clip.X + clip.Width),(dx + (((pathBounds.X + pathBounds.Width)) * sx))), Math::Min((clip.Y + clip.Height),(dy + (((pathBounds.Y + pathBounds.Height)) * sy))));
}
}
activePaintState = stateId;
activeVisible = visible;
if(placement.Decorations) continue;
if(box.TextRunShaped) {
double em = (Scale(styles[id]) * fontSize);
int firstRunPath = (int)paths.size();
for(auto runPath : box.TextRunShape.Paths) {
MathBounds clip = MathBounds{((px * fontSize) + (runPath.ClipBounds.X * em)),((py * fontSize) + (runPath.ClipBounds.Y * em)),(runPath.ClipBounds.Width * em),(runPath.ClipBounds.Height * em)};
paths.push_back(MathPath{runPath.Segments,((px * fontSize) + (runPath.X * em)),((py * fontSize) + (runPath.Y * em)),(runPath.ScaleX * em),(runPath.ScaleY * em),clip});
AddPaint(MathPaintKind::Path, ((int)paths.size() - 1));
}
MathBounds runInk = box.TextRunShape.InkBounds;
AddInk(((px * fontSize) + (runInk.X * em)), ((py * fontSize) + (runInk.Y * em)), ((px * fontSize) + (((runInk.X + runInk.Width)) * em)), ((py * fontSize) + (((runInk.Y + runInk.Height)) * em)));
textRuns.push_back(MathTextRun{box.RunText,(px * fontSize),(py * fontSize),em,firstRunPath,((int)paths.size() - firstRunPath),(box.TextRunShape.Advance * em)});
continue;
}
switch(node.Kind){
case NodeKind::Symbol:
case NodeKind::Delimiter:
case NodeKind::Operator:
{
if(box.TextShaped) {
MathTextShape shape = box.TextShape;
double size = (Scale(styles[id]) * fontSize);
MathBounds clip = MathBounds{((px * fontSize) + (shape.InkBounds.X * size)),((py * fontSize) + (shape.InkBounds.Y * size)),(shape.InkBounds.Width * size),(shape.InkBounds.Height * size)};
paths.push_back(MathPath{shape.Segments,(px * fontSize),(py * fontSize),size,size,clip});
AddPaint(MathPaintKind::Path, ((int)paths.size() - 1));
textGlyphs.push_back(MathTextGlyph{node.CodePoint,(px * fontSize),(py * fontSize),size,shape.FontFamily});
AddInk(clip.X, clip.Y, (clip.X + clip.Width), (clip.Y + clip.Height));
}
if(box.Symbol) EmitGlyph(box.Font, box.GlyphIndex, FontData::Glyphs[box.GlyphIndex].CodePoint, px, (py + box.GlyphY), ((box.GlyphScale == 0) ? Scale(styles[id]) : box.GlyphScale));
if(((node.Kind == NodeKind::Operator) && (node.FirstChild >= 0))) for(int child = node.FirstChild;(child >= 0);child = nodes[child].NextSibling) Place(child);
break;
}
case NodeKind::Space:
break;
case NodeKind::Row:
case NodeKind::Style:
case NodeKind::Text:
case NodeKind::Matrix:
case NodeKind::MatrixRow:
case NodeKind::MatrixCell:
case NodeKind::Color:
case NodeKind::Sizing:
{
int length = Collect(nodes, node.FirstChild, sequence, collectStack, styles);
if(((node.Kind == NodeKind::MatrixRow) && (node.SecondChild >= 0))) Place(node.SecondChild);
for(int i = (length - 1);(i >= 0);i--) Place(sequence[i]);
break;
}
case NodeKind::Font:
case NodeKind::Delimited:
Place(node.FirstChild);
break;
case NodeKind::Class:
for(int child = node.FirstChild;(child >= 0);child = nodes[child].NextSibling) Place(child);
break;
case NodeKind::Choice:
Place(((styles[id].Style < 2) ? node.FirstChild : ((styles[id].Style < 4) ? node.SecondChild : ((styles[id].Style < 6) ? node.ThirdChild : node.FourthChild))));
break;
case NodeKind::MathMlChoice:
Place(node.FirstChild);
break;
case NodeKind::Box:
{
if(((node.Text == u"\\reflectbox") || (node.Text == u"\\mathreflectbox"))) {
MathTransform reflection = MathTransform{-1,0,0,1,((((2 * px) + box.Width)) * fontSize),0};
int reflected = GetState(([&]{auto copy = currentState;copy.Transform=Then(reflection, currentState.Transform);return copy;}()));
placements[placementCount++] = Placement{node.FirstChild,(px + x[node.FirstChild]),(py + y[node.FirstChild]),reflected};
} else if((node.Text == u"\\pmb")) {
Place(node.FirstChild);
double em = (Scale(styles[id]) * fontSize);
int shadow = GetState(([&]{auto copy = currentState;copy.Transform=Then(MathTransform{1,0,0,1,(.02 * em),(.01 * em)}, currentState.Transform);return copy;}()));
placements[placementCount++] = Placement{node.FirstChild,(px + x[node.FirstChild]),(py + y[node.FirstChild]),shadow};
} else for(int child = node.FirstChild;(child >= 0);child = nodes[child].NextSibling) Place(child);
break;
}
case NodeKind::Enclose:
Place(node.FirstChild);
break;
case NodeKind::Tag:
if((node.SecondChild >= 0)) Place(node.SecondChild);
Place(node.FirstChild);
break;
case NodeKind::Accent:
if((node.SecondChild >= 0)) Place(node.SecondChild);
if((node.FirstChild >= 0)) Place(node.FirstChild);
break;
case NodeKind::Script:
if((node.ThirdChild >= 0)) Place(node.ThirdChild);
if((node.SecondChild >= 0)) Place(node.SecondChild);
if((node.FirstChild >= 0)) Place(node.FirstChild);
break;
case NodeKind::Fraction:
{
double rx = (((px + box.PathX)) * fontSize), ry = (((py + box.PathY)) * fontSize);
double rw = (box.PathScale * fontSize), rh = (box.PathHeight * fontSize);
if((node.HasBar && (rh > 0))) {
double paintedHeight = Math::Max(1,Math::Floor(rh));
double paintedY = ((ry + rh) - paintedHeight);
rules.push_back(MathRule{rx,paintedY,rw,paintedHeight});
AddPaint(MathPaintKind::Rule, ((int)rules.size() - 1));
if((rw > 0)) AddInk(rx, paintedY, (rx + rw), (paintedY + paintedHeight));
}
Place(node.SecondChild);
Place(node.FirstChild);
break;
}
case NodeKind::Radical:
{
double rx = (((px + box.PathX)) * fontSize), ry = (((py + box.PathY)) * fontSize);
double ps = (box.PathScale * fontSize);
double radicalOffset = box.PathX;
auto clip = MathBounds{rx,ry,(((box.Width - radicalOffset)) * fontSize),(box.PathHeight * fontSize)};
paths.push_back(MathPath{box.Path,rx,ry,ps,ps,clip});
AddPaint(MathPaintKind::Path, ((int)paths.size() - 1));
MathBounds pb = PathBounds(box.Path);
AddInk(Math::Max(clip.X,(rx + (pb.X * ps))), Math::Max(clip.Y,(ry + (pb.Y * ps))), Math::Min((clip.X + clip.Width),(rx + (((pb.X + pb.Width)) * ps))), Math::Min((clip.Y + clip.Height),(ry + (((pb.Y + pb.Height)) * ps))));
if((node.SecondChild >= 0)) Place(node.SecondChild);
Place(node.FirstChild);
break;
}
}


}
auto bounds = ((inkLeft==INFINITY) ? MathBounds{0,0,0,0} : MathBounds{inkLeft,inkTop,(inkRight - inkLeft),(inkBottom - inkTop)});
LayoutOutput output = LayoutOutput{std::move(glyphs),std::move(rules),std::move(paths),MathSize{(Math::Max(0,outer.Width) * fontSize),(((outer.Height + outer.Depth)) * fontSize)},(outer.Height * fontSize),bounds,std::move(textGlyphs),std::move(paintCommands),std::move(paintStates),std::move(textRuns)};
glyphs.clear();
rules.clear();
paths.clear();
textGlyphs.clear();
textRuns.clear();
paintCommands.clear();
paintStates.clear();

decorations.clear();
return output;




}}
Box Layout::Symbol(const SemanticNode& node, double scale, IMathTextShaper* textShaper){ MathTextRunShape singleRun{};TexGlyphMetric runMetric{};int glyphIndex{};TexGlyphMetric metric{};MathTextShape shape{};TexGlyphMetric fallback{};TexGlyphMetric sourceMetric{};{
int defaultFont = ((node.SymbolFont == MathFont::AMS) ? 0 : (node.IsMathOrd ? 10 : 8));
int desired = ([&]() -> int { auto sw12 = node.Font; if((sw12 == MathFont::MathItalic)) return 7; if((sw12 == MathFont::MathNormal)) return 10; if(((sw12 == MathFont::Main) || (sw12 == MathFont::MathRoman))) return 8; if((sw12 == MathFont::MathBold)) return 5; if((sw12 == MathFont::MainBoldItalic)) return 6; if((sw12 == MathFont::MathBoldItalic)) return (node.IsMathOrd ? 9 : 5); if((sw12 == MathFont::MathSans)) return 13; if((sw12 == MathFont::MathSansBold)) return 11; if((sw12 == MathFont::MathSansItalic)) return 12; if((sw12 == MathFont::MathSansBoldItalic)) return 8; if((sw12 == MathFont::MathTypewriter)) return 19; if((sw12 == MathFont::AMS)) return 0; if((sw12 == MathFont::Caligraphic)) return 2; if((sw12 == MathFont::Fraktur)) return 4; if((sw12 == MathFont::Script)) return 14; if(true) return defaultFont; throw std::logic_error("exhaustive switch"); }());
if((node.FontIdOverride >= 0)) desired = node.FontIdOverride;
int metricFont = desired;
if((node.TextMode && (node.FontIdOverride == 10))) desired = 7;
if((((node.OriginalCodePoint >= 0x1d468) && (node.OriginalCodePoint < 0x1d49c)) && (desired == 6))) desired = 9;
if((((node.OriginalCodePoint >= 0x1d56c) && (node.OriginalCodePoint < 0x1d5a0)) && (desired == 4))) desired = 3;
if((((node.FontIdOverride < 0) && (node.Atom != MathAtom::Ord)) && !((node.Font == MathFont::MathBoldItalic)))) desired = defaultFont;
MathFont shapeFont = (((desired == 10) || (desired == 7)) ? MathFont::MathItalic : ((desired == 9) ? MathFont::MathBoldItalic : node.Font));
if(((((node.TextMode && Contextual(node.CodePoint)) && (textShaper != nullptr && textShaper->SupportsRuns())) && textShaper->TryShapeRun(utf16(node.CodePoint), shapeFont, singleRun)) && ValidTextRun(singleRun, 16))) {
if((!FallbackMetricData::Find(desired, node.CodePoint, true, runMetric) && (((node.CodePoint >= 0x0530) && (node.CodePoint <= 0x058f)) || ((node.CodePoint >= 0x0900) && (node.CodePoint <= 0x10ff))))) Find(desired, u'M', _, runMetric);
return ([&]{Box v{};v.Width=(singleRun.Advance * scale);v.Height=(runMetric.Height * scale);v.Depth=(runMetric.Depth * scale);v.Atom=node.Atom;v.TextRunShaped=true;v.TextRunShape=singleRun;v.RunText=utf16(node.CodePoint);return v;}());
}
if(!Find(desired, node.CodePoint, glyphIndex, metric)) {
if(((node.Font == MathFont::MathBoldItalic) && Find(5, node.CodePoint, glyphIndex, metric))) desired = 5; else if(Find(defaultFont, node.CodePoint, glyphIndex, metric)) desired = defaultFont; else if((((((((((((!(!textShaper) && textShaper->TryShape(node.CodePoint, shapeFont, shape)) && std::isfinite(shape.Advance)) && (shape.Advance >= 0)) && (shape.Advance <= 16)) && !shape.Segments.empty()) && std::isfinite(shape.InkBounds.X)) && std::isfinite(shape.InkBounds.Y)) && std::isfinite(shape.InkBounds.Width)) && std::isfinite(shape.InkBounds.Height)) && (shape.InkBounds.Width >= 0)) && (shape.InkBounds.Height >= 0))) {
if(((!FallbackMetricData::Find(desired, node.CodePoint, node.TextMode, fallback) && node.TextMode) && ((((((((((node.CodePoint >= 0x0100) && (node.CodePoint <= 0x024f)) || ((node.CodePoint >= 0x0300) && (node.CodePoint <= 0x036f))) || ((node.CodePoint >= 0x0400) && (node.CodePoint <= 0x04ff))) || ((node.CodePoint >= 0x0530) && (node.CodePoint <= 0x058f))) || ((node.CodePoint >= 0x0900) && (node.CodePoint <= 0x10ff))) || ((node.CodePoint >= 0x3000) && (node.CodePoint <= 0x30ff))) || ((node.CodePoint >= 0x4e00) && (node.CodePoint <= 0x9faf))) || ((node.CodePoint >= 0xff00) && (node.CodePoint <= 0xff60))) || ((node.CodePoint >= 0xac00) && (node.CodePoint <= 0xd7af))))) Find(desired, u'M', _, fallback);
double fallbackItalic = ((node.TextMode || (node.Font == MathFont::MathItalic)) ? 0 : Math::Max(0,fallback.ItalicCorrection));
return ([&]{Box v{};v.Width=(((shape.Advance + fallbackItalic)) * scale);v.Height=(fallback.Height * scale);v.Depth=(fallback.Depth * scale);v.Italic=(fallbackItalic * scale);v.Atom=node.Atom;v.TextShaped=true;v.TextShape=shape;return v;}());
} else throw CompileFailure{MathDiagnostic{MathDiagnosticCode::UnknownSymbol,node.Range,std::u16string(u"Locked font data has no glyph.")}};
}
FontGlyph glyph = FontData::Glyphs[glyphIndex];
if(((metricFont != desired) && FallbackMetricData::Find(metricFont, node.CodePoint, node.TextMode, sourceMetric))) metric = sourceMetric;
double italic = ((node.TextMode || (node.Font == MathFont::MathItalic)) ? 0 : metric.ItalicCorrection);
return ([&]{Box v{};v.Width=((((glyph.AdvanceUnits / (double)(FontData::Fonts[desired].UnitsPerEm)) + Math::Max(0,italic))) * scale);v.Height=(metric.Height * scale);v.Depth=(metric.Depth * scale);v.Italic=(italic * scale);v.Skew=(metric.Skew * scale);v.Atom=node.Atom;v.Character=true;v.Symbol=true;v.Font=desired;v.GlyphIndex=glyphIndex;return v;}());
}}
int Layout::Collect(std::span<const SemanticNode> nodes, int first, std::vector<int>& destination, std::vector<int>& stack, std::vector<LayoutContext>& contexts){ {
int length = 0, depth = 0, current = first;
while(((current >= 0) || (depth > 0))) {
if((current < 0)) {
current = stack[--depth];
continue;
}
const SemanticNode& node = nodes[current];
if((((((((node.Kind == NodeKind::Style) || (node.Kind == NodeKind::Sizing)) || (node.Kind == NodeKind::Color)) || (node.Kind == NodeKind::Choice)) || (node.Kind == NodeKind::MathMlChoice)) || ((node.Kind == NodeKind::Row) && (node.Value == 2))) || ((node.Kind == NodeKind::Box) && ((node.Text == u"\\phantom") || (node.Text == u"\\hbox"))))) {
if((node.NextSibling >= 0)) stack[depth++] = node.NextSibling;
current = ((node.Kind == NodeKind::Choice) ? ((contexts[current].Style < 2) ? node.FirstChild : ((contexts[current].Style < 4) ? node.SecondChild : ((contexts[current].Style < 6) ? node.ThirdChild : node.FourthChild))) : node.FirstChild);
if(((((((((node.Kind == NodeKind::Style) || (node.Kind == NodeKind::Sizing)) || (node.Kind == NodeKind::Color)) || (node.Kind == NodeKind::Choice)) || (node.Kind == NodeKind::MathMlChoice)) && (current >= 0)) && (nodes[current].Kind == NodeKind::Row)) && (nodes[current].NextSibling < 0))) current = nodes[current].FirstChild;
} else {
destination[length++] = current;
current = node.NextSibling;
}
}
return length;
}}
double Layout::Scale(uint8_t style) { return ((style >= 6) ? 0.5 : ((style >= 4) ? 0.7 : 1)); }
int Layout::MetricIndex(uint8_t style) { return ((style >= 6) ? 2 : ((style >= 4) ? 1 : 0)); }
bool Layout::CancelsBinOnLeft(MathAtom atom) { return (((((atom == MathAtom::Bin) || (atom == MathAtom::Open)) || (atom == MathAtom::Rel)) || (atom == MathAtom::Op)) || (atom == MathAtom::Punct)); }
bool Layout::CancelsBinOnRight(MathAtom atom) { return (((atom == MathAtom::Rel) || (atom == MathAtom::Close)) || (atom == MathAtom::Punct)); }
int Layout::Spacing(MathAtom left, MathAtom right, bool tight){ {
if(tight) return ([&]() -> int { auto sw13 = std::make_tuple(left,right); if(((((((std::get<0>(sw13) == MathAtom::Ord) && (std::get<1>(sw13) == MathAtom::Op)) || ((std::get<0>(sw13) == MathAtom::Op) && (std::get<1>(sw13) == MathAtom::Ord))) || ((std::get<0>(sw13) == MathAtom::Op) && (std::get<1>(sw13) == MathAtom::Op))) || ((std::get<0>(sw13) == MathAtom::Close) && (std::get<1>(sw13) == MathAtom::Op))) || ((std::get<0>(sw13) == MathAtom::Inner) && (std::get<1>(sw13) == MathAtom::Op)))) return 3; if(true) return 0; throw std::logic_error("exhaustive switch"); }());
return ([&]() -> int { auto sw14 = std::make_tuple(left,right); if(((((((((((((((((((std::get<0>(sw14) == MathAtom::Ord) && (std::get<1>(sw14) == MathAtom::Op)) || ((std::get<0>(sw14) == MathAtom::Ord) && (std::get<1>(sw14) == MathAtom::Inner))) || ((std::get<0>(sw14) == MathAtom::Op) && (std::get<1>(sw14) == MathAtom::Ord))) || ((std::get<0>(sw14) == MathAtom::Op) && (std::get<1>(sw14) == MathAtom::Op))) || ((std::get<0>(sw14) == MathAtom::Op) && (std::get<1>(sw14) == MathAtom::Inner))) || ((std::get<0>(sw14) == MathAtom::Close) && (std::get<1>(sw14) == MathAtom::Op))) || ((std::get<0>(sw14) == MathAtom::Close) && (std::get<1>(sw14) == MathAtom::Inner))) || ((std::get<0>(sw14) == MathAtom::Punct) && (std::get<1>(sw14) == MathAtom::Ord))) || ((std::get<0>(sw14) == MathAtom::Punct) && (std::get<1>(sw14) == MathAtom::Op))) || ((std::get<0>(sw14) == MathAtom::Punct) && (std::get<1>(sw14) == MathAtom::Open))) || ((std::get<0>(sw14) == MathAtom::Punct) && (std::get<1>(sw14) == MathAtom::Close))) || ((std::get<0>(sw14) == MathAtom::Punct) && (std::get<1>(sw14) == MathAtom::Punct))) || ((std::get<0>(sw14) == MathAtom::Punct) && (std::get<1>(sw14) == MathAtom::Inner))) || ((std::get<0>(sw14) == MathAtom::Inner) && (std::get<1>(sw14) == MathAtom::Ord))) || ((std::get<0>(sw14) == MathAtom::Inner) && (std::get<1>(sw14) == MathAtom::Op))) || ((std::get<0>(sw14) == MathAtom::Inner) && (std::get<1>(sw14) == MathAtom::Open))) || ((std::get<0>(sw14) == MathAtom::Inner) && (std::get<1>(sw14) == MathAtom::Punct)))) return 3; if(((((((((std::get<0>(sw14) == MathAtom::Ord) && (std::get<1>(sw14) == MathAtom::Bin)) || ((std::get<0>(sw14) == MathAtom::Bin) && (std::get<1>(sw14) == MathAtom::Ord))) || ((std::get<0>(sw14) == MathAtom::Bin) && (std::get<1>(sw14) == MathAtom::Op))) || ((std::get<0>(sw14) == MathAtom::Bin) && (std::get<1>(sw14) == MathAtom::Open))) || ((std::get<0>(sw14) == MathAtom::Bin) && (std::get<1>(sw14) == MathAtom::Inner))) || ((std::get<0>(sw14) == MathAtom::Close) && (std::get<1>(sw14) == MathAtom::Bin))) || ((std::get<0>(sw14) == MathAtom::Inner) && (std::get<1>(sw14) == MathAtom::Bin)))) return 4; if(((((((((((std::get<0>(sw14) == MathAtom::Ord) && (std::get<1>(sw14) == MathAtom::Rel)) || ((std::get<0>(sw14) == MathAtom::Op) && (std::get<1>(sw14) == MathAtom::Rel))) || ((std::get<0>(sw14) == MathAtom::Rel) && (std::get<1>(sw14) == MathAtom::Ord))) || ((std::get<0>(sw14) == MathAtom::Rel) && (std::get<1>(sw14) == MathAtom::Op))) || ((std::get<0>(sw14) == MathAtom::Rel) && (std::get<1>(sw14) == MathAtom::Open))) || ((std::get<0>(sw14) == MathAtom::Rel) && (std::get<1>(sw14) == MathAtom::Inner))) || ((std::get<0>(sw14) == MathAtom::Close) && (std::get<1>(sw14) == MathAtom::Rel))) || ((std::get<0>(sw14) == MathAtom::Punct) && (std::get<1>(sw14) == MathAtom::Rel))) || ((std::get<0>(sw14) == MathAtom::Inner) && (std::get<1>(sw14) == MathAtom::Rel)))) return 5; if(true) return 0; throw std::logic_error("exhaustive switch"); }());
}}
int Layout::SelectRadical(double height){ TexGlyphMetric metric{};{
for(int size = 0;(size < 5);size++) {
int font = ((size == 0) ? 8 : (size + 14));
if((Find(font, 0x221a, _, metric) && ((metric.Height + metric.Depth) > height))) return size;
}
return 5;
}}
MathBounds Layout::PathBounds(std::span<const MathPathSegment> segments){ {
double minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
double x = 0, y = 0, sx = 0, sy = 0;
auto Include = [&](double px,double py){
minX = Math::Min(minX,px);
minY = Math::Min(minY,py);
maxX = Math::Max(maxX,px);
maxY = Math::Max(maxY,py);
};
auto CubicExtrema = [&](double p0,double p1,double p2,double p3,bool horizontal){
double a = (((-p0 + (3 * p1)) - (3 * p2)) + p3), b = (2 * (((p0 - (2 * p1)) + p2))), c = (p1 - p0);
auto Evaluate = [&](double t){
if(((t <= 0) || (t >= 1))) return;
double u = (1 - t), p = ((((((u * u) * u) * p0) + ((((3 * u) * u) * t) * p1)) + ((((3 * u) * t) * t) * p2)) + (((t * t) * t) * p3));
if(horizontal) {
minX = Math::Min(minX,p);
maxX = Math::Max(maxX,p);
} else {
minY = Math::Min(minY,p);
maxY = Math::Max(maxY,p);
}
};
if((Math::Abs(a) < 1e-12)) {
if((Math::Abs(b) > 1e-12)) Evaluate((-c / b));
return;
}
double discriminant = ((b * b) - ((4 * a) * c));
if((discriminant < 0)) return;
double r = Math::Sqrt(discriminant);
Evaluate((((-b + r)) / ((2 * a))));
Evaluate((((-b - r)) / ((2 * a))));

};
for(auto segment : segments) {
switch(segment.Operation){
case MathPathOperation::Move:
x = sx = segment.X1;
y = sy = segment.Y1;
Include(x, y);
break;
case MathPathOperation::Line:
x = segment.X1;
y = segment.Y1;
Include(x, y);
break;
case MathPathOperation::Cubic:
CubicExtrema(x, segment.X1, segment.X2, segment.X3, true);
CubicExtrema(y, segment.Y1, segment.Y2, segment.Y3, false);
x = segment.X3;
y = segment.Y3;
Include(x, y);
break;
case MathPathOperation::Close:
x = sx;
y = sy;
Include(x, y);
break;
}
}
return MathBounds{minX,minY,(maxX - minX),(maxY - minY)};


}}
void Layout::TryShapeTextRow(std::span<const SemanticNode> nodes, int id, LayoutContext context, const MathOptions& options, std::vector<std::optional<MathColor>>& colors, std::vector<Box>& boxes, std::vector<double>& x){ MathTextRunShape shape{};{
if(!options.TextShaper || !options.TextShaper->SupportsRuns() || nodes[id].FirstChild<0)return;
double correction = 0;
for(int child = nodes[id].FirstChild;(child >= 0);) {
const SemanticNode& node = nodes[child];
x[child] += correction;
if(((((node.Kind != NodeKind::Symbol) || !node.TextMode) || (node.OriginalCodePoint > 0)) || !Contextual(node.CodePoint))) {
child = node.NextSibling;
continue;
}
MathFont font = node.Font;
std::optional<MathColor> color = colors[child];
int next = node.NextSibling;
std::u16string text;
text.append(utf16(node.CodePoint));
double previousAdvance = boxes[child].Width;
while((((((((next >= 0) && (nodes[next].Kind == NodeKind::Symbol)) && nodes[next].TextMode) && (nodes[next].OriginalCodePoint == 0)) && (nodes[next].Font == font)) && (colors[next] == color)) && Contextual(nodes[next].CodePoint))) {
text.append(utf16(nodes[next].CodePoint));
previousAdvance += boxes[next].Width;
next = nodes[next].NextSibling;
}
std::u16string value = text;
if((!options.TextShaper->TryShapeRun(value, font, shape) || !ValidTextRun(shape, (options.MaxInputLength * 16)))) throw CompileFailure{MathDiagnostic{MathDiagnosticCode::UnknownSymbol,node.Range,u"The text run could not be shaped by the configured platform provider."}};
Box& first = boxes[child];
first.Width = (shape.Advance * context.Scale);
first.TextRunShaped = true;
first.TextRunShape = shape;
first.RunText = value;
for(int consumed = nodes[child].NextSibling;((consumed >= 0) && (consumed != next));consumed = nodes[consumed].NextSibling) {
boxes[consumed].TextShaped = false;
boxes[consumed].TextRunShaped = false;
boxes[consumed].Symbol = false;
boxes[consumed].Width = 0;
}
correction += ((shape.Advance * context.Scale) - previousAdvance);
child = next;
}
boxes[id].Width += correction;
}}
bool Layout::Contextual(int cp) { return (((((((((cp >= 0x0530) && (cp <= 0x08ff)) || ((cp >= 0x0900) && (cp <= 0x10ff))) || ((cp >= 0x1100) && (cp <= 0x11ff))) || ((cp >= 0xa960) && (cp <= 0xa97f))) || ((cp >= 0xd7b0) && (cp <= 0xd7ff))) || ((cp >= 0x0300) && (cp <= 0x036f))) || (cp == 0x200c)) || (cp == 0x200d)); }
bool Layout::ValidTextRun(const MathTextRunShape& shape, double maximumAdvance) { return (((((((((std::isfinite(shape.Advance) && (shape.Advance >= 0)) && (shape.Advance <= maximumAdvance)) && std::isfinite(shape.InkBounds.X)) && std::isfinite(shape.InkBounds.Y)) && std::isfinite(shape.InkBounds.Width)) && std::isfinite(shape.InkBounds.Height)) && (shape.InkBounds.Width >= 0)) && (shape.InkBounds.Height >= 0)) && ((!shape.Paths.empty() || ((shape.InkBounds.Width == 0) && (shape.InkBounds.Height == 0))))); }
