// KaTeX-compatible rendering of enclosures, rules and equation tags.
//
// Port of KaTeX 0.19.0 `src/functions/enclose.ts`, `src/functions/rule.ts`
// and `src/functions/tag.ts`.  The vertical layout helpers come from
// `build_common.hpp`, which is itself a port of KaTeX's `src/buildCommon.ts`.
#include "enclose.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "../types.hpp"
#include "build_common.hpp"
#include "domtree.hpp"
#include "jsnum.hpp"
#include "options.hpp"

namespace velocitex::html {
namespace {

// KaTeX's label tests in `src/functions/enclose.ts` are regular expressions
// over the command name without the leading backslash.
bool MatchesCancel(const std::string& label) {
    return label.find("cancel") != std::string::npos;
}

bool MatchesBox(const std::string& label) {
    return label.find("box") != std::string::npos;
}

// `/fbox|boxed|fcolorbox/` -- note that "fcolorbox" does not contain "fbox".
bool MatchesFbox(const std::string& label) {
    return same_text(label, "fbox") || same_text(label, "boxed") || same_text(label, "fcolorbox");
}

// KaTeX's `isCharacterBox` (`src/utils.ts`).
//
// `getBaseElem` unwraps an ordgroup or a color group that holds a single
// child and then a font group; the innermost node is a character box when it
// is a plain atom.
bool IsCharacterBox(const ModuleContext& context, int index) {
    if (index < 0) {
        return false;
    }
    const SemanticNode& node = context.Node(index);
    switch (node.Kind) {
    case NodeKind::Symbol:
        return true;
    case NodeKind::Row:
    case NodeKind::Color: {
        const IndexVector children = context.Children(index);
        if (children.size() != 1) {
            return false;
        }
        return IsCharacterBox(context, children.front());
    }
    case NodeKind::Font:
        // A font group holds the (normalised) argument directly.
        return IsCharacterBox(context, node.FirstChild);
    default:
        return false;
    }
}

// KaTeX's `wrapFragment` (`src/buildCommon.ts`).
//
// The shared helper in `build_common.hpp` drops the options, so the "mtight"
// class and the current colour would be lost; KaTeX passes them on.
HtmlNodePtr WrapFragmentWithOptions(HtmlNodePtr group, const HtmlOptions& options) {
    if (group->Kind() == HtmlNodeKind::Fragment) {
        NodeVector children;
        children.push_back(std::move(group));
        return MakeSpan({}, std::move(children), options);
    }
    return group;
}

// KaTeX's `phasePath` (`src/svgGeometry.ts`).
std::string PhasePath(double view_box_height) {
    const double x = view_box_height / 2.0;
    return "M400000 " + JsNumberToString(view_box_height) + " H0 L" + JsNumberToString(x) + " 0 l65 45 L145 " +
           JsNumberToString(view_box_height - 80.0) + " H400000z";
}

// KaTeX's `stretchyEnclose` (`src/stretchy.ts`).
//
// Returns the image span that carries the frame or the strike-through line.
//
// KaTeX writes the label of the frame as the class of the span.  The labels
// that carry a frame are a fixed set, so the names are entries of a table: a
// class list holds them as views, and a view of the label of the caller
// would dangle when the caller returns.
std::string_view FrameClassName(const std::string& label) {
    if (same_text(label, "fbox")) {
        return "fbox";
    }
    if (same_text(label, "colorbox")) {
        return "colorbox";
    }
    if (same_text(label, "fcolorbox")) {
        return "fcolorbox";
    }
    if (same_text(label, "angl")) {
        return "angl";
    }
    return CopyToNodeArena(label);
}

HtmlNodePtr MakeStretchyEnclose(const HtmlNodePtr& inner, const std::string& label, double top_pad,
                                double bottom_pad, const HtmlOptions& options) {
    const double total_height = inner->Height + inner->Depth + top_pad + bottom_pad;
    HtmlNodePtr img;
    if (label.find("fbox") != std::string::npos || label.find("color") != std::string::npos ||
        label.find("angl") != std::string::npos) {
        img = MakeSpan({"katex-stretchy", FrameClassName(label)}, {}, options);
        if (same_text(label, "fbox")) {
            // KaTeX: `options.color && options.getColor()`.
            if (options.HasColor) {
                if (const std::optional<std::string_view> color = options.GetColor()) {
                    img->Style.Set("border-color", *color);
                }
            }
        }
    } else {
        // \cancel, \bcancel or \xcancel.  The stroke width does not vary with
        // the span area because the SVG has no viewBox.
        NodeVector lines;
        if (same_text(label, "bcancel") || same_text(label, "xcancel")) {
            auto line = MakeNode<HtmlLine>();
            line->SetAttribute("x1", "0");
            line->SetAttribute("y1", "0");
            line->SetAttribute("x2", "100%");
            line->SetAttribute("y2", "100%");
            line->SetAttribute("stroke-width", "0.046em");
            lines.push_back(std::move(line));
        }
        if (same_text(label, "cancel") || same_text(label, "xcancel")) {
            auto line = MakeNode<HtmlLine>();
            line->SetAttribute("x1", "0");
            line->SetAttribute("y1", "100%");
            line->SetAttribute("x2", "100%");
            line->SetAttribute("y2", "0");
            line->SetAttribute("stroke-width", "0.046em");
            lines.push_back(std::move(line));
        }
        auto svg = MakeNode<HtmlSvg>();
        svg->SetAttribute("width", "100%");
        svg->SetAttributeEm("height", total_height);
        svg->Children() = std::move(lines);
        NodeVector children;
        children.push_back(std::move(svg));
        img = MakeSpan({}, std::move(children), options);
    }
    img->Height = total_height;
    img->Style.SetEm("height", total_height);
    return img;
}

// KaTeX's `htmlBuilder` for the "enclose" node (`src/functions/enclose.ts`).
HtmlNodePtr BuildEnclose(const ModuleContext& context, int index, const HtmlOptions& options) {
    const SemanticNode& node = context.Node(index);
    const std::string label = utf8(text(node.Text)).substr(1);

    HtmlNodePtr inner = context.build_group(node.FirstChild, options);
    // KaTeX's `html.buildGroup` returns a document fragment for a "styling"
    // group -- the node that KaTeX's "hbox" argument type creates -- and
    // `wrapFragment` moves the fragment's children into a fresh span.  The C++
    // core returns the styled group directly, so the wrapper is created here.
    if (node.FirstChild >= 0 && context.Node(node.FirstChild).Kind == NodeKind::Style) {
        NodeVector children;
        children.push_back(std::move(inner));
        inner = MakeSpan({}, std::move(children), options);
    } else {
        inner = WrapFragmentWithOptions(std::move(inner), options);
    }

    double scale = options.SizeMultiplier;
    HtmlNodePtr img;
    double img_shift = 0.0;

    // In the LaTeX cancel package, line geometry is slightly different
    // depending on whether the subject is wider than it is tall, or vice
    // versa.  We don't know the width of a group, so as a proxy, we test if
    // the subject is a single character.
    const bool is_single_char = IsCharacterBox(context, node.FirstChild);

    const bool has_background_color = node.Text2.has_value();
    const bool has_border_color = node.Text3.has_value();

    if (same_text(label, "sout")) {
        img = MakeSpan({"katex-stretchy", "katex-sout"}, {});
        img->Height = options.Metrics().DefaultRuleThickness / scale;
        img_shift = -0.5 * options.Metrics().XHeight;
    } else if (same_text(label, "phase")) {
        // Set a couple of dimensions from the steinmetz package.
        const double line_weight = CalculateSize(0.6, TexUnit::Pt, options);
        const double clearance = CalculateSize(0.35, TexUnit::Ex, options);

        // Prevent size changes like \Huge from affecting line thickness.
        const HtmlOptions new_options = options.HavingBaseSizing();
        scale = scale / new_options.SizeMultiplier;

        const double angle_height = inner->Height + inner->Depth + line_weight + clearance;
        // Reserve a left pad for the angle.
        inner->Style.SetEm("padding-left", angle_height / 2.0 + line_weight);

        // Create an SVG.
        const double view_box_height = std::floor(1000.0 * angle_height * scale);
        auto svg = MakeNode<HtmlSvg>();
        svg->SetAttribute("width", "400em");
        svg->SetAttributeEm("height", view_box_height / 1000.0);
        svg->SetAttribute("viewBox", "0 0 400000 " + JsNumberToString(view_box_height));
        svg->SetAttribute("preserveAspectRatio", "xMinYMin slice");
        NodeVector svg_children;
        svg_children.push_back(MakeNode<HtmlPath>(PhasePath(view_box_height)));
        svg->Children() = std::move(svg_children);
        // Wrap it in a span with overflow: hidden.
        NodeVector children;
        children.push_back(std::move(svg));
        img = MakeSpan({"hide-tail"}, std::move(children), options);
        img->Style.SetEm("height", angle_height);
        img_shift = inner->Depth + line_weight + clearance;
    } else {
        // Add horizontal padding.
        if (MatchesCancel(label)) {
            if (!is_single_char) {
                inner->AddClass("cancel-pad");
            }
        } else if (same_text(label, "angl")) {
            inner->AddClass("anglpad");
        } else {
            inner->AddClass("boxpad");
        }

        // Add vertical padding.
        double top_pad = 0.0;
        double bottom_pad = 0.0;
        double rule_thickness = 0.0;
        // ref: cancel package: \advance\totalheight2\p@ % "+2"
        if (MatchesBox(label)) {
            rule_thickness = std::max(options.Metrics().Fboxrule, options.MinRuleThickness);
            top_pad = options.Metrics().Fboxsep + (same_text(label, "colorbox") ? 0.0 : rule_thickness);
            bottom_pad = top_pad;
        } else if (same_text(label, "angl")) {
            rule_thickness = std::max(options.Metrics().DefaultRuleThickness, options.MinRuleThickness);
            top_pad = 4.0 * rule_thickness;  // gap = 3 x line, plus the line itself.
            bottom_pad = std::max(0.0, 0.25 - inner->Depth);
        } else {
            top_pad = is_single_char ? 0.2 : 0.0;
            bottom_pad = top_pad;
        }

        img = MakeStretchyEnclose(inner, label, top_pad, bottom_pad, options);
        if (MatchesFbox(label)) {
            img->Style.Set("border-style", "solid");
            img->Style.SetEm("border-width", rule_thickness);
        } else if (same_text(label, "angl") && rule_thickness != 0.049) {
            img->Style.SetEm("border-top-width", rule_thickness);
            img->Style.SetEm("border-right-width", rule_thickness);
        }
        img_shift = inner->Depth + bottom_pad;

        if (has_background_color) {
            img->Style.Set("background-color", utf8(*node.Text2));
            if (has_border_color) {
                img->Style.Set("border-color", utf8(*node.Text3));
            }
        }
    }

    const double inner_height = inner->Height;
    const double inner_depth = inner->Depth;

    VListParams params;
    params.PositionType = VListPositionType::IndividualShift;
    if (has_background_color) {
        // Put the color background behind inner.
        VListElement image;
        image.Elem = std::move(img);
        image.Shift = img_shift;
        params.ShiftedElements.push_back(std::move(image));
        VListElement content;
        content.Elem = std::move(inner);
        content.Shift = 0.0;
        params.ShiftedElements.push_back(std::move(content));
    } else {
        // Write the \cancel stroke on top of inner.
        VListElement content;
        content.Elem = std::move(inner);
        content.Shift = 0.0;
        params.ShiftedElements.push_back(std::move(content));
        VListElement image;
        image.Elem = std::move(img);
        image.Shift = img_shift;
        if (MatchesCancel(label) || same_text(label, "phase")) {
            image.WrapperClasses = {"svg-align"};
        }
        params.ShiftedElements.push_back(std::move(image));
    }
    NodePtr<HtmlSpan> vlist = MakeVList(std::move(params), options);

    if (MatchesCancel(label)) {
        // The cancel package documentation says that cancel lines add their
        // height to the expression, but tests show that isn't how it actually
        // works.
        vlist->Height = inner_height;
        vlist->Depth = inner_depth;
    }

    NodeVector children;
    children.push_back(std::move(vlist));
    if (MatchesCancel(label) && !is_single_char) {
        // cancel does not create horiz space for its line extension.
        return MakeSpan({"mord", "cancel-lap"}, std::move(children), options);
    }
    return MakeSpan({"mord"}, std::move(children), options);
}

// KaTeX's `htmlBuilder` for the "rule" node (`src/functions/rule.ts`).
HtmlNodePtr BuildRule(const ModuleContext& context, int index, const HtmlOptions& options) {
    const SemanticNode& node = context.Node(index);
    // Make an empty span for the rule.
    auto rule = MakeSpan({"mord", "katex-rule"}, {}, options);

    // Calculate the shift, width, and height of the rule, and account for
    // units.
    const double width = CalculateSize(node.Length1.Number, node.Length1.Unit, options);
    const double height = CalculateSize(node.Length2.Number, node.Length2.Unit, options);
    const double shift = CalculateSize(node.Length3.Number, node.Length3.Unit, options);

    // Style the rule to the right size.
    rule->Style.SetEm("border-right-width", width);
    rule->Style.SetEm("border-top-width", height);
    rule->Style.SetEm("bottom", shift);

    // Record the height and width.
    rule->Width = width;
    rule->HasWidth = true;
    rule->Height = height + shift;
    rule->Depth = -shift;
    // Font size is the number large enough that the browser will reserve at
    // least `absHeight` space above the baseline.  The 1.125 factor was
    // empirically determined.
    rule->MaxFontSize = height * 1.125 * options.SizeMultiplier;

    return rule;
}

// KaTeX renders the equation tag in `buildHTML` (`src/buildHTML.ts`): the tag
// body is turned into an unbreakable chunk whose classes are replaced by
// "katex-tag".
//
// KaTeX's `buildHTML` additionally stretches the tag's strut to the height of
// the whole `katex-html` node; that size is not known inside a group builder,
// so the strut keeps the height of the tag itself.
HtmlNodePtr BuildTag(const ModuleContext& context, int index, const HtmlOptions& options) {
    const SemanticNode& node = context.Node(index);
    NodeVector parts;
    parts.push_back(context.build_group(node.SecondChild, options));

    auto body = MakeSpan({"katex-base"}, std::move(parts), options);
    auto strut = MakeSpan({"katex-strut"}, {});
    strut->Style.SetEm("height", body->Height + body->Depth);
    if (body->Depth != 0.0) {
        strut->Style.SetEm("vertical-align", -body->Depth);
    }
    NodeVector children;
    children.push_back(std::move(strut));
    for (auto& part : body->Children()) {
        children.push_back(std::move(part));
    }
    body->Children() = std::move(children);
    body->Classes = "katex-tag";
    return body;
}

}  // namespace

HtmlNodePtr BuildEncloseNode(const ModuleContext& context, int index, const HtmlOptions& options) {
    const SemanticNode& node = context.Node(index);
    switch (node.Kind) {
    case NodeKind::Enclose:
        return BuildEnclose(context, index, options);
    case NodeKind::Rule:
        return BuildRule(context, index, options);
    case NodeKind::Tag:
        // The parser stores the equation tag in a `NodeKind::Tag` node that
        // wraps the whole equation, but the top level builder renders the
        // children of the root row only, so this node is never reached.
        return BuildTag(context, index, options);
    default:
        break;
    }
    return MakeSpan({}, {});
}

}  // namespace velocitex::html
