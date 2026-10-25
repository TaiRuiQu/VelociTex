// KaTeX-compatible HTML renderer.
//
// Port of KaTeX 0.19.0 `src/buildHTML.ts` plus the HTML builders of the
// `src/functions/*` modules, operating on VelociTex's semantic tree.
#include "build_html.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "accent.hpp"
#include "array.hpp"
#include "build_common.hpp"
#include "delimiter.hpp"
#include "domtree.hpp"
#include "enclose.hpp"
#include "jsnum.hpp"
#include "module_api.hpp"
#include "options.hpp"
#include "svg.hpp"

#include "../parser.hpp"

namespace velocitex::html {
namespace {

// KaTeX's `\oiint` and `\oiiint` are the two operators that are drawn as an
// integral with an overlaid oval (`src/functions/op.ts`).
bool IsOiintLabel(std::u16string_view label) {
    return label == u"\\oiint"_u16 || label == u"\\oiiint"_u16;
}

// KaTeX's math class names, keyed by the semantic tree's atom kind.
const char* AtomClass(MathAtom atom) {
    switch (atom) {
        case MathAtom::Ord: return "mord";
        case MathAtom::Op: return "mop";
        case MathAtom::Bin: return "mbin";
        case MathAtom::Rel: return "mrel";
        case MathAtom::Open: return "mopen";
        case MathAtom::Close: return "mclose";
        case MathAtom::Punct: return "mpunct";
        case MathAtom::Inner: return "minner";
    }
    return "mord";
}

bool IsMathClass(std::string_view value) {
    return same_text(value, "mord") || same_text(value, "mop") || same_text(value, "mbin") ||
           same_text(value, "mrel") || same_text(value, "mopen") || same_text(value, "mclose") ||
           same_text(value, "mpunct") || same_text(value, "minner");
}

// KaTeX's inter-atom spacing table, expressed in mu.
int SpacingBetween(std::string_view left, std::string_view right, bool tight) {
    if (tight) {
        if (same_text(left, "mord") && same_text(right, "mop")) return 3;
        if (same_text(left, "mop") && (same_text(right, "mord") || same_text(right, "mop"))) return 3;
        if (same_text(left, "mclose") && same_text(right, "mop")) return 3;
        if (same_text(left, "minner") && same_text(right, "mop")) return 3;
        return 0;
    }
    if (same_text(left, "mord")) {
        if (same_text(right, "mop") || same_text(right, "minner")) return 3;
        if (same_text(right, "mbin")) return 4;
        if (same_text(right, "mrel")) return 5;
    } else if (same_text(left, "mop")) {
        if (same_text(right, "mord") || same_text(right, "mop") || same_text(right, "minner")) return 3;
        if (same_text(right, "mrel")) return 5;
    } else if (same_text(left, "mbin")) {
        if (same_text(right, "mord") || same_text(right, "mop") || same_text(right, "mopen") ||
            same_text(right, "minner"))
            return 4;
    } else if (same_text(left, "mrel")) {
        if (same_text(right, "mord") || same_text(right, "mop") || same_text(right, "mopen") ||
            same_text(right, "minner"))
            return 5;
    } else if (same_text(left, "mclose")) {
        if (same_text(right, "mop") || same_text(right, "minner")) return 3;
        if (same_text(right, "mbin")) return 4;
        if (same_text(right, "mrel")) return 5;
    } else if (same_text(left, "mpunct")) {
        if (same_text(right, "mord") || same_text(right, "mop") || same_text(right, "mopen") ||
            same_text(right, "mclose") || same_text(right, "mpunct") || same_text(right, "minner"))
            return 3;
        if (same_text(right, "mrel")) return 5;
    } else if (same_text(left, "minner")) {
        if (same_text(right, "mord") || same_text(right, "mop") || same_text(right, "mopen") ||
            same_text(right, "mpunct") || same_text(right, "minner"))
            return 3;
        if (same_text(right, "mbin")) return 4;
        if (same_text(right, "mrel")) return 5;
    }
    return 0;
}

// KaTeX's `makeNullDelimiter`.
NodePtr<HtmlSpan> MakeNullDelimiter(const HtmlOptions& options, ClassList&& classes) {
    classes.Add("nulldelimiter");
    options.AddBaseSizingClasses(classes);
    return MakeSpan(std::move(classes), {});
}

std::string ToUtf8(std::u16string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        int cp = text[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 &&
            text[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            ++i;
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// A node is a "partial group" when it must not affect the spacing around it.
bool IsPartialGroup(const HtmlNode& node) {
    if (node.Kind() == HtmlNodeKind::Fragment || node.Kind() == HtmlNodeKind::Anchor) {
        return true;
    }
    return node.Kind() == HtmlNodeKind::Span && node.HasClass("enclosing");
}

// Returns the outermost node at the given side of a (possibly partial) group.
const HtmlNode* OutermostNode(const HtmlNode& node, bool right) {
    if (IsPartialGroup(node)) {
        const auto& children = node.Children();
        if (!children.empty()) {
            return OutermostNode(*children[right ? children.size() - 1 : 0], right);
        }
    }
    return &node;
}

// KaTeX's `getTypeOfDomTree`.
std::string_view TypeOfDomTree(const HtmlNode& node) {
    if (!node.Classes.Empty() && IsMathClass(node.Classes.Front())) {
        return node.Classes.Front();
    }
    return {};
}

std::string_view TypeOfDomTreeAt(const HtmlNode& node, bool right) {
    const HtmlNode* outermost = OutermostNode(node, right);
    if (!outermost->Classes.Empty() && IsMathClass(outermost->Classes.Front())) {
        return outermost->Classes.Front();
    }
    return {};
}

}  // namespace

// The builder walks the semantic tree and produces KaTeX's HTML DOM tree.
class Builder {
public:
    Builder(std::span<const SemanticNode> semantic, int root, const MathOptions& options)
        : semantic_(semantic), root_(root), options_(options) {}

    void Build(MarkupBuffer& out);

private:
    const SemanticNode& Node(int index) const { return semantic_[static_cast<size_t>(index)]; }
    IndexVector Children(int index) const;

    HtmlNodePtr BuildGroup(int index, const HtmlOptions& options, const HtmlOptions* base_options);
    NodeVector BuildExpression(const IndexVector& nodes, const HtmlOptions& options,
                                             bool is_real_group,
                                             std::string_view surrounding_prev = "leftmost",
                                             std::string_view surrounding_next = "rightmost",
                                             bool is_root = false);
    NodePtr<HtmlSpan> BuildUnbreakable(NodeVector& parts, const HtmlOptions& options);

    HtmlNodePtr BuildSymbol(int index, const HtmlOptions& options);
    HtmlNodePtr BuildSpace(int index, const HtmlOptions& options);
    HtmlNodePtr BuildRow(int index, const HtmlOptions& options, bool as_ord_group);
    HtmlNodePtr BuildScript(int index, const HtmlOptions& options);
    // KaTeX's generic supsub builder, with the base of the scripts replaced.
    // The accent builder uses it to attach the scripts to the accent's base.
    HtmlNodePtr BuildScriptWithBase(int index, const HtmlOptions& options, int base_index);
    HtmlNodePtr BuildFraction(int index, const HtmlOptions& options);
    HtmlNodePtr BuildStyle(int index, const HtmlOptions& options);
    // KaTeX's `sizingGroup`.
    HtmlNodePtr SizingGroup(const IndexVector& children, const HtmlOptions& new_options,
                            const HtmlOptions& base_options);
    HtmlNodePtr BuildFont(int index, const HtmlOptions& options);
    HtmlNodePtr BuildText(int index, const HtmlOptions& options);
    HtmlNodePtr BuildColor(int index, const HtmlOptions& options);
    HtmlNodePtr BuildClass(int index, const HtmlOptions& options);
    HtmlNodePtr BuildBox(int index, const HtmlOptions& options);
    HtmlNodePtr BuildSizing(int index, const HtmlOptions& options);
    HtmlNodePtr BuildChoice(int index, const HtmlOptions& options);
    HtmlNodePtr BuildVerb(int index, const HtmlOptions& options);
    HtmlNodePtr BuildOperator(int index, const HtmlOptions& options);
    // Builds the operator without the axis shift; `BuildOperatorLimits` needs
    // the unshifted base because KaTeX places the shift inside the vlist.
    HtmlNodePtr BuildOperatorBase(int index, const HtmlOptions& options);
    HtmlNodePtr BuildOperatorLimits(int index, const HtmlOptions& options, int sup_index, int sub_index);
    HtmlNodePtr BuildRadical(int index, const HtmlOptions& options);
    HtmlNodePtr BuildDelimited(int index, const HtmlOptions& options);
    HtmlNodePtr BuildDelimiter(int index, const HtmlOptions& options);
    std::u16string CollectText(int index) const;
    // KaTeX's `isCharacterBox`: a group that boils down to a single atom.
    // Such a base does not shift its scripts (TeXbook rule 18a/18b).
    bool IsCharacterBox(int index) const;
    // True when a script has no base of its own.
    bool IsMissingBase(int index) const;
    // KaTeX's `ordargument`: the body of a group is the content of its
    // ordgroup, or the group itself when it is not an ordgroup.
    IndexVector OrdArgument(int index) const;
    // KaTeX's function arguments are always groups (`parseGroup` returns an
    // ordgroup), so a single argument renders as a "mord" span.
    HtmlNodePtr BuildOrdGroup(int index, const HtmlOptions& options);
    // True when the style node holds the style of a fraction, which KaTeX
    // applies to the fraction itself (`\dfrac`, `\tfrac` and `\cfrac`).
    bool IsGenfracStyle(int index) const;
    // Callback handed to the renderer modules so that they can build child
    // groups with KaTeX's `buildGroup` semantics.
    BuildGroupFn BuildGroupCallback();

    std::span<const SemanticNode> semantic_;
    int root_{0};
    const MathOptions& options_;
    // The `\middle` delimiters of the group that is currently being built.
    // KaTeX keeps the delimiter information on the rendered node so that the
    // `\left`/`\right` builder can resize them.
    std::vector<std::pair<const HtmlNode*, std::u16string>> middle_delimiters_;
};

// Collects the characters of a text-mode subtree.  KaTeX renders text mode
// content as a single symbol, with spaces turned into non-breaking spaces.
std::u16string Builder::CollectText(int index) const {
    std::u16string out;
    for (const int child : Children(index)) {
        const SemanticNode& node = Node(child);
        if (node.Kind == NodeKind::Symbol) {
            if (node.CodePoint > 0) {
                if (node.CodePoint == ' ') {
                    out.push_back(0x00A0);
                } else {
                    out.push_back(static_cast<char16_t>(node.CodePoint));
                }
            }
        } else if (node.Kind == NodeKind::Space) {
            out.push_back(0x00A0);
        } else if (node.Kind == NodeKind::Row) {
            out += CollectText(child);
        }
    }
    return out;
}

IndexVector Builder::Children(int index) const {
    IndexVector result;
    if (index < 0) {
        return result;
    }
    // The children are counted before the list is filled.  The list lives in
    // the arena of the nodes, and the growth of the vector would allocate and
    // copy the list again for every step.
    size_t count = 0;
    for (int child = Node(index).FirstChild; child >= 0; child = Node(child).NextSibling) {
        ++count;
    }
    result.reserve(count);
    for (int child = Node(index).FirstChild; child >= 0; child = Node(child).NextSibling) {
        result.push_back(child);
    }
    return result;
}

// Callback handed to the renderer modules so that they can build child
// groups with KaTeX's `buildGroup` semantics.
BuildGroupFn Builder::BuildGroupCallback() {
    return [this](int index, const HtmlOptions& options) { return BuildGroup(index, options, nullptr); };
}

// KaTeX's `isCharacterBox` (`src/utils.ts`) unwraps an ordgroup or a color
// group that holds a single child and then checks whether the innermost node
// is a plain atom.  Only such a base skips the base dependent part of the
// script shifts.
bool Builder::IsCharacterBox(int index) const {    if (index < 0) {
        return false;
    }
    const SemanticNode& node = Node(index);
    switch (node.Kind) {
    case NodeKind::Symbol:
        return true;
    case NodeKind::Row:
    case NodeKind::Color: {
        const IndexVector children = Children(index);
        if (children.size() != 1) {
            return false;
        }
        return IsCharacterBox(children.front());
    }
    case NodeKind::Font:
        // KaTeX's `getBaseElem` unwraps a font group as well.
        return IsCharacterBox(node.FirstChild);
    default:
        return false;
    }
}

// The parser stores an empty row as the base of a script that has no base of
// its own.  Such a row is not an ordgroup, so KaTeX renders the base as an
// empty span without any class (`html.buildGroup(null)`).
bool Builder::IsMissingBase(int index) const {
    if (index < 0) {
        return true;
    }
    const SemanticNode& node = Node(index);
    return node.Kind == NodeKind::Row && node.Value == 0 && node.FirstChild < 0;
}

// True when the style node holds the style of a fraction.  KaTeX's
// `\dfrac`, `\tfrac` and `\cfrac` change the style of the fraction itself
// instead of wrapping it in a styling group.
bool Builder::IsGenfracStyle(int index) const {
    if (index < 0) {
        return false;
    }
    const SemanticNode& node = Node(index);
    return node.Kind == NodeKind::Style && node.FirstChild >= 0 &&
           Node(node.FirstChild).Kind == NodeKind::Fraction;
}

// KaTeX's `ordargument` (`src/defineFunction.ts`): the body of a group is
// the content of its ordgroup, or the group itself when it is not an
// ordgroup.
IndexVector Builder::OrdArgument(int index) const {
    if (index < 0) {
        return {};
    }
    if (Node(index).Kind == NodeKind::Row) {
        return Children(index);
    }
    return {index};
}

// KaTeX's function arguments are always groups (`Parser.parseGroup` returns
// an ordgroup), so a single argument renders as a "mord" span.  The C++ tree
// stores the group as a Row node; an argument that was a single node has no
// Row, so the group is recreated here.
HtmlNodePtr Builder::BuildOrdGroup(int index, const HtmlOptions& options) {
    if (index < 0) {
        return MakeSpan({}, {});
    }
    if (Node(index).Kind == NodeKind::Row) {
        return BuildGroup(index, options, nullptr);
    }
    NodeVector children;
    children.push_back(BuildGroup(index, options, nullptr));
    return MakeSpan({"mord"}, std::move(children), options);
}

NodeVector Builder::BuildExpression(const IndexVector& nodes, const HtmlOptions& options,
                                                   bool is_real_group, std::string_view surrounding_prev,
                                                   std::string_view surrounding_next, bool is_root) {
    NodeVector groups;
    // The number of groups is at least the number of nodes, so reserving it
    // avoids the repeated reallocation of the result vector.
    groups.reserve(nodes.size());
    for (const int index : nodes) {
        HtmlNodePtr output = BuildGroup(index, options, nullptr);
        if (output->Kind() == HtmlNodeKind::Fragment) {
            for (auto& child : output->Children()) {
                groups.push_back(std::move(child));
            }
        } else {
            groups.push_back(std::move(output));
        }
    }

    groups = TryCombineChars(std::move(groups));

    if (!is_real_group) {
        return groups;
    }

    HtmlOptions glue_options = options;
    if (nodes.size() == 1) {
        const SemanticNode& node = Node(nodes.front());
        if (node.Kind == NodeKind::Sizing) {
            glue_options = options.HavingSize(static_cast<int>(node.Value));
        } else if (node.Kind == NodeKind::Style) {
            Style style = options.CurrentStyle;
            switch (node.Style) {
                case MathStyle::Display: style = Style(0, 0, false); break;
                case MathStyle::Text: style = Style(2, 1, false); break;
                case MathStyle::Script: style = Style(4, 2, false); break;
                case MathStyle::ScriptScript: style = Style(6, 3, false); break;
            }
            glue_options = options.HavingStyle(style);
        }
    }

    // The cursor tracks the atom that was seen last.  KaTeX starts the walk
    // with a dummy "leftmost" atom, so the cursor refers to a null node until
    // the first real atom shows up.
    struct Cursor {
        HtmlNode* node = nullptr;
        std::string_view dummy_class;
    };
    auto raw_class = [](const HtmlNode* node, std::string_view dummy) -> std::string_view {
        if (node == nullptr) {
            return dummy;
        }
        return node->Classes.Empty() ? std::string_view{} : node->Classes.Front();
    };
    auto math_class = [&raw_class](const Cursor& cursor) -> std::string_view {
        const std::string_view value = raw_class(cursor.node, cursor.dummy_class);
        return IsMathClass(value) ? value : std::string_view{};
    };

    // Bin cancellation: binary operators change to ordinary symbols in some
    // contexts.  KaTeX's `traverseNonSpaceNodes` (`src/buildHTML.ts`) walks the
    // node list depth first, descending into partial groups, and keeps the
    // previously seen node in a cursor that is shared with the recursion.
    auto cancel_bin = [](HtmlNode* node, std::string_view node_class, HtmlNode* prev,
                         std::string_view prev_class) {
        if (prev != nullptr && same_text(prev_class, "mbin") &&
            (same_text(node_class, "rightmost") || same_text(node_class, "mrel") ||
             same_text(node_class, "mclose") || same_text(node_class, "mpunct"))) {
            prev->Classes.SetFront("mord");
        } else if (node != nullptr && same_text(node_class, "mbin") &&
                   (same_text(prev_class, "leftmost") || same_text(prev_class, "mbin") ||
                    same_text(prev_class, "mopen") || same_text(prev_class, "mrel") ||
                    same_text(prev_class, "mop") || same_text(prev_class, "mpunct"))) {
            node->Classes.SetFront("mord");
        }
    };
    Cursor cursor;
    cursor.dummy_class = surrounding_prev;
    auto traverse_bins = [&](auto&& self, NodeVector& nodes) -> void {
        for (auto& node : nodes) {
            if (IsPartialGroup(*node)) {
                NodeVector children = std::move(node->Children());
                self(self, children);
                node->Children() = std::move(children);
                continue;
            }
            if (node->HasClass("mspace")) {
                // A line break starts a new line, so the atom before it
                // does not influence the spacing of the next one.
                if (is_root && node->HasClass("katex-newline")) {
                    cursor.node = nullptr;
                }
                continue;
            }
            cancel_bin(node.get(), raw_class(node.get(), {}), cursor.node, raw_class(cursor.node, cursor.dummy_class));
            cursor.node = node.get();
        }
    };
    traverse_bins(traverse_bins, groups);
    // KaTeX appends the dummy "rightmost" span to the node list, so a
    // trailing binary operator is cancelled as well.
    {
        const std::string_view next_class = surrounding_next;
        cancel_bin(nullptr, next_class, cursor.node, raw_class(cursor.node, cursor.dummy_class));
    }

    // Insert the glue between atoms of different classes.
    cursor.node = nullptr;
    auto traverse_spaces = [&](auto&& self, NodeVector& nodes) -> void {
        for (size_t i = 0; i < nodes.size(); ++i) {
            HtmlNode* node = nodes[i].get();
            if (IsPartialGroup(*node)) {
                NodeVector children = std::move(node->Children());
                self(self, children);
                node->Children() = std::move(children);
                continue;
            }
            if (node->HasClass("mspace")) {
                if (is_root && node->HasClass("katex-newline")) {
                    cursor.node = nullptr;
                }
                continue;
            }
            const std::string_view previous_type = math_class(cursor);
            const std::string_view type = TypeOfDomTree(*node);
            if (!previous_type.empty() && !type.empty()) {
                const int space = SpacingBetween(previous_type, type, node->HasClass("mtight"));
                if (space != 0) {
                    const double size = space * glue_options.Metrics().CssEmPerMu;
                    // KaTeX inserts the glue *before* the current node.
                    nodes.insert(nodes.begin() + static_cast<long>(i), MakeGlue(size, glue_options));
                    ++i;
                }
            }
            cursor.node = node;
        }
    };
    traverse_spaces(traverse_spaces, groups);

    return groups;
}

NodePtr<HtmlSpan> Builder::BuildUnbreakable(NodeVector& parts, const HtmlOptions& options) {
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
    return body;
}

HtmlNodePtr Builder::BuildGroup(int index, const HtmlOptions& options, const HtmlOptions* base_options) {
    if (index < 0) {
        return MakeSpan({}, {});
    }
    const SemanticNode& node = Node(index);
    HtmlNodePtr result;
    switch (node.Kind) {
        case NodeKind::Symbol: result = BuildSymbol(index, options); break;
        case NodeKind::Space: result = BuildSpace(index, options); break;
        case NodeKind::Row: result = BuildRow(index, options, true); break;
        case NodeKind::Script: result = BuildScript(index, options); break;
        case NodeKind::Fraction: result = BuildFraction(index, options); break;
        case NodeKind::Style: result = BuildStyle(index, options); break;
        case NodeKind::Font: result = BuildFont(index, options); break;
        case NodeKind::Text: result = BuildText(index, options); break;
        case NodeKind::Color: result = BuildColor(index, options); break;
        case NodeKind::Class: result = BuildClass(index, options); break;
        case NodeKind::Box: result = BuildBox(index, options); break;
        case NodeKind::Sizing: result = BuildSizing(index, options); break;
        case NodeKind::Choice:
        case NodeKind::MathMlChoice: result = BuildChoice(index, options); break;
        case NodeKind::Verb: result = BuildVerb(index, options); break;
        case NodeKind::Operator: result = BuildOperator(index, options); break;
        case NodeKind::Radical: result = BuildRadical(index, options); break;
        case NodeKind::Delimited: result = BuildDelimited(index, options); break;
        case NodeKind::Delimiter: result = BuildDelimiter(index, options); break;
        case NodeKind::Accent:
            result = BuildAccentNode(ModuleContext{semantic_, BuildGroupCallback()}, index, options);
            break;
        case NodeKind::Matrix:
            result = BuildArrayNode(ModuleContext{semantic_, BuildGroupCallback()}, index, options);
            break;
        case NodeKind::Enclose:
        case NodeKind::Rule:
        case NodeKind::Tag:
            result = BuildEncloseNode(ModuleContext{semantic_, BuildGroupCallback()}, index, options);
            break;
        default: result = MakeSpan({}, {}); break;
    }
    if (base_options != nullptr && options.Size != base_options->Size) {
        NodeVector children;
        children.push_back(std::move(result));
        ClassList sizing_classes;
        options.AddSizingClasses(sizing_classes, *base_options);
        auto wrapper = MakeSpan(std::move(sizing_classes), std::move(children), options);
        const double multiplier = options.SizeMultiplier / base_options->SizeMultiplier;
        wrapper->Height *= multiplier;
        wrapper->Depth *= multiplier;
        return wrapper;
    }
    return result;
}

HtmlNodePtr Builder::BuildSymbol(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    const Mode mode = node.TextMode ? Mode::Text : Mode::Math;
    // KaTeX keeps the mathematical alphanumeric symbols as a surrogate pair in
    // the parse tree; `makeOrd` then resolves the font and the narrow
    // replacement character.
    const int scalar = node.OriginalCodePoint > 0 ? node.OriginalCodePoint : node.CodePoint;
    // The text of the symbol is written into a buffer of its own: a formula has
    // hundreds of symbols, and a string for each of them asks the library for
    // the storage.
    char16_t buffer[2];
    const std::u16string_view value = scalar > 0 ? velocitex::ToScalar(scalar, buffer) : std::u16string_view();
    // KaTeX's space symbols (`\ `, `~`, `\space` and `\nobreakspace`) belong
    // to the "spacing" group and are rendered with the "mspace" class.
    const bool is_spacing = node.CodePoint == 0x00A0;
    const bool nobreak = is_spacing && node.Text.has_value() && *node.Text == u"\\nobreakspace"_u16;
    if (mode == Mode::Math && is_spacing) {
        NodeVector children;
        children.push_back(MathSym(value, mode, options, {}, node.SymbolFont));
        ClassList classes{"mspace"};
        if (nobreak) {
            classes.Add("nobreak");
        }
        return MakeSpan(std::move(classes), std::move(children), options);
    }
    if (node.Atom == MathAtom::Ord) {
        OrdSpec spec;
        spec.Text = value;
        spec.TextModeValue = mode;
        spec.IsMathOrd = node.IsMathOrd;
        spec.SymbolFont = node.SymbolFont;
        HtmlNodePtr ord = MakeOrd(spec, options);
        if (nobreak) {
            ord->AddClass("nobreak");
        }
        return ord;
    }
    // A non-ordinal atom: KaTeX renders it through `mathsym`.
    return MathSym(value, mode, options, {AtomClass(node.Atom)}, node.SymbolFont);
}

HtmlNodePtr Builder::BuildSpace(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    if (node.Text.has_value()) {
        const std::u16string& label = *node.Text;
        if (label == u"\\nobreak"_u16 || label == u"\\allowbreak"_u16) {
            return MakeSpan({"mspace", label == u"\\nobreak"_u16 ? "nobreak" : "allowbreak"}, {}, options);
        }
        if (label == u"\\\\"_u16) {
            // KaTeX's `cr` builder: a line break is an empty mspace that
            // carries the "katex-newline" class.  The optional size of the
            // break becomes its top margin.
            ClassList classes{"mspace"};
            HtmlNodePtr span = MakeSpan(std::move(classes), {}, options);
            if (node.Value != 0.0) {
                span->AddClass("katex-newline");
                if (node.Length1.Number != 0.0 || node.Length1.Unit != TexUnit::Em) {
                    span->Style.SetEm("margin-top",
                                      CalculateSize(node.Length1.Number, node.Length1.Unit, options));
                }
            }
            return span;
        }
        if (label == u"\\nobreakspace"_u16) {
            NodeVector nb; nb.push_back(MathSym(u" ", Mode::Math, options)); return MakeSpan({"mspace", "nobreak"}, std::move(nb));
        }
        if (label == u" "_u16 || label == u"\\ "_u16 || label == u"~"_u16 || label == u"\\space"_u16) {
            if (node.TextMode) {
                OrdSpec spec;
                spec.Text = u" ";
                spec.TextModeValue = Mode::Text;
                spec.IsMathOrd = false;
                HtmlNodePtr ord = MakeOrd(spec, options);
                if (label == u"~"_u16 || label == u"\\nobreakspace"_u16) {
                    ord->AddClass("nobreak");
                }
                return ord;
            }
            ClassList classes{"mspace"};
            if (label == u"~"_u16) {
                classes.Add("nobreak");
            }
            NodeVector children;
            children.push_back(MathSym(u" ", Mode::Math, options));
            return MakeSpan(std::move(classes), std::move(children));
        }
    }
    // A glue or kern of a fixed size.
    double size = node.Value;
    if (node.Text.has_value() && (*node.Text == u"\\kern"_u16 || *node.Text == u"\\mkern"_u16 ||
                                  *node.Text == u"\\hskip"_u16 || *node.Text == u"\\mskip"_u16)) {
        // `\kern`, `\mkern`, `\hskip` and `\mskip` carry a TeX length.
        size = CalculateSize(node.Length1.Number, node.Length1.Unit, options);
    }
    return MakeGlue(size, options);
}

HtmlNodePtr Builder::BuildRow(int index, const HtmlOptions& options, bool as_ord_group) {
    IndexVector children = Children(index);
    NodeVector inner = BuildExpression(children, options, as_ord_group);
    if (!as_ord_group) {
        return MakeFragment(std::move(inner));
    }
    // KaTeX's ordgroup builder wraps the group in a "mord" span.
    return MakeSpan({"mord"}, std::move(inner), options);
}

HtmlNodePtr Builder::BuildScript(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    const bool has_sub = node.ThirdChild >= 0;
    // KaTeX's `htmlBuilderDelegate` (`src/functions/supsub.ts`): some groups
    // handle their own super/subscripts.
    if (node.FirstChild >= 0) {
        const SemanticNode& base_node = Node(node.FirstChild);
        if (base_node.Kind == NodeKind::Operator) {
            const bool display = options.CurrentStyle.Size == 0;
            if (text(base_node.Text) == u"\\operatorname"_u16) {
                // KaTeX's `operatorname` builder only takes the scripts when
                // the operator always handles them (`\operatorname*`).
                const bool always = base_node.Text2.has_value() && *base_node.Text2 == u"star"_u16;
                if (always && (display || base_node.Limits)) {
                    return BuildOperatorLimits(node.FirstChild, options, node.SecondChild, node.ThirdChild);
                }
            } else {
                const bool delegate = base_node.Limits && (display || base_node.AlwaysLimits);
                if (delegate) {
                    return BuildOperatorLimits(node.FirstChild, options, node.SecondChild, node.ThirdChild);
                }
            }
        } else if (base_node.Kind == NodeKind::Accent) {
            const std::u16string_view label = text(base_node.Text);
            BuildGroupFn build_group = BuildGroupCallback();
            ModuleContext context{semantic_, build_group};
            if (IsHorizBraceLabel(label)) {
                const bool is_over = label.find(u"\\over"_u16) != std::u16string_view::npos;
                // The brace takes the script when it is on the same side.
                if ((!has_sub) == is_over) {
                    // KaTeX builds the script with the base options, so that the
                    // size change is applied to the rendered group.
                    HtmlNodePtr script;
                    if (node.SecondChild >= 0) {
                        script = BuildGroup(node.SecondChild, options.HavingStyle(options.CurrentStyle.Sup()),
                                            &options);
                    } else if (node.ThirdChild >= 0) {
                        script = BuildGroup(node.ThirdChild, options.HavingStyle(options.CurrentStyle.Sub()), &options);
                    }
                    return BuildHorizBraceNode(context, node.FirstChild, options, std::move(script));
                }
            } else if (IsCharacterBox(base_node.FirstChild)) {
                // KaTeX's accent builder rebuilds the supsub with the accent's
                // base and then replaces the base with the accent itself, so
                // that the scripts do not move with the accent's height.
                const int accent_base = NormalizeAccentBase(context, base_node.FirstChild);
                HtmlNodePtr supsub = BuildScriptWithBase(index, options, accent_base);
                HtmlNodePtr accent = BuildAccentNode(context, node.FirstChild, options);
                supsub->Children()[0] = std::move(accent);
                supsub->Height = std::max(supsub->Height, supsub->Children()[0]->Height);
                if (!supsub->Classes.Empty()) {
                    supsub->Classes.SetFront("mord");
                }
                return supsub;
            }
        }
    }
    return BuildScriptWithBase(index, options, node.FirstChild);
}

HtmlNodePtr Builder::BuildScriptWithBase(int index, const HtmlOptions& options, int base_index) {
    const SemanticNode& node = Node(index);
    HtmlNodePtr base;
    if (IsMissingBase(base_index)) {
        // KaTeX has no base at all when a script starts a group; its
        // `buildGroup(null)` returns an empty span without any class.
        base = MakeSpan({}, {});
    } else {
        base = BuildGroup(base_index, options, nullptr);
    }
    const GlobalMetrics& metrics = options.Metrics();
    double sup_shift = 0.0;
    double sub_shift = 0.0;
    HtmlNodePtr sup;
    HtmlNodePtr sub;
    const bool has_sup = node.SecondChild >= 0;
    const bool has_sub = node.ThirdChild >= 0;
    // Rule 18a/18b: a character box keeps its scripts at the fixed distance
    // instead of following the height and depth of the base.
    const bool is_char_box = IsCharacterBox(base_index);
    if (has_sup) {
        const HtmlOptions new_options = options.HavingStyle(options.CurrentStyle.Sup());
        sup = BuildGroup(node.SecondChild, new_options, &options);
        if (!is_char_box) {
            sup_shift = base->Height - new_options.Metrics().SupDrop * new_options.SizeMultiplier /
                                           options.SizeMultiplier;
        }
    }
    if (has_sub) {
        const HtmlOptions new_options = options.HavingStyle(options.CurrentStyle.Sub());
        sub = BuildGroup(node.ThirdChild, new_options, &options);
        if (!is_char_box) {
            sub_shift = base->Depth + new_options.Metrics().SubDrop * new_options.SizeMultiplier /
                                          options.SizeMultiplier;
        }
    }
    double min_sup_shift = metrics.Sup2;
    if (options.CurrentStyle.Id == 0) {
        min_sup_shift = metrics.Sup1;
    } else if (options.CurrentStyle.Cramped) {
        min_sup_shift = metrics.Sup3;
    }
    const double margin_right = (0.5 / metrics.PtPerEm) / options.SizeMultiplier;
    std::optional<double> margin_left;
    if (sub && base->Kind() == HtmlNodeKind::Symbol) {
        margin_left = -static_cast<HtmlSymbol&>(*base).ItalicValue();
    } else if (sub && base_index >= 0 && Node(base_index).Kind == NodeKind::Operator &&
               IsOiintLabel(text(Node(base_index).Text))) {
        // `\oiint` and `\oiiint` are spans that carry the italic correction of
        // the integral they are built from (`src/functions/supsub.ts`).
        margin_left = -base->Italic;
    }
    NodePtr<HtmlSpan> supsub;
    if (sup && sub) {
        sup_shift = std::max({sup_shift, min_sup_shift, sup->Depth + 0.25 * metrics.XHeight});
        sub_shift = std::max(sub_shift, metrics.Sub2);
        const double rule_width = metrics.DefaultRuleThickness;
        const double max_width = 4 * rule_width;
        if ((sup_shift - sup->Depth) - (sub->Height - sub_shift) < max_width) {
            sub_shift = max_width - (sup_shift - sup->Depth) + sub->Height;
            const double psi = 0.8 * metrics.XHeight - (sup_shift - sup->Depth);
            if (psi > 0) {
                sup_shift += psi;
                sub_shift -= psi;
            }
        }
        VListParams params;
        params.PositionType = VListPositionType::IndividualShift;
        VListElement sub_element;
        sub_element.Elem = std::move(sub);
        sub_element.Shift = sub_shift;
        sub_element.MarginRight = margin_right;
        if (margin_left.has_value()) {
            sub_element.MarginLeft = *margin_left;
        }
        VListElement sup_element;
        sup_element.Elem = std::move(sup);
        sup_element.Shift = -sup_shift;
        sup_element.MarginRight = margin_right;
        params.ShiftedElements.push_back(std::move(sub_element));
        params.ShiftedElements.push_back(std::move(sup_element));
        supsub = MakeVList(std::move(params), options);
    } else if (sub) {
        sub_shift = std::max({sub_shift, metrics.Sub1, sub->Height - 0.8 * metrics.XHeight});
        VListParams params;
        params.PositionType = VListPositionType::Shift;
        params.PositionData = sub_shift;
        VListElement element;
        element.Elem = std::move(sub);
        element.MarginRight = margin_right;
        if (margin_left.has_value()) {
            element.MarginLeft = *margin_left;
        }
        params.Elements.push_back(std::move(element));
        supsub = MakeVList(std::move(params), options);
    } else if (sup) {
        sup_shift = std::max({sup_shift, min_sup_shift, sup->Depth + 0.25 * metrics.XHeight});
        VListParams params;
        params.PositionType = VListPositionType::Shift;
        params.PositionData = -sup_shift;
        VListElement element;
        element.Elem = std::move(sup);
        element.MarginRight = margin_right;
        params.Elements.push_back(std::move(element));
        supsub = MakeVList(std::move(params), options);
    } else {
        return base;
    }
    std::string_view mclass;
    {
        const std::string_view view = TypeOfDomTreeAt(*base, true);
        mclass = view.empty() ? "mord" : view;
    }
    if (mclass.empty()) {
        mclass = "mord";
    }
    NodeVector inner;
    inner.push_back(std::move(supsub));
    auto msupsub = MakeSpan({"msupsub"}, std::move(inner));
    NodeVector children;
    children.push_back(std::move(base));
    children.push_back(std::move(msupsub));
    // KaTeX wraps the base and the scripts in a span with the base's math
    // class, using the current options so that the span inherits `mtight`.
    return MakeSpan({mclass}, std::move(children), options);
}

HtmlNodePtr Builder::BuildFraction(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    const Style& style = options.CurrentStyle;

    // Rule 15(a): the numerator and denominator use their own styles.
    const HtmlOptions numerator_options = options.HavingStyle(style.FracNum());
    const HtmlOptions denominator_options = options.HavingStyle(style.FracDen());
    HtmlNodePtr numerator = BuildGroup(node.FirstChild, numerator_options, &options);
    HtmlNodePtr denominator = BuildGroup(node.SecondChild, denominator_options, &options);

    const GlobalMetrics& metrics = options.Metrics();
    // `\cfrac` inserts a strut into the numerator; the strut dimensions come
    // from the TeXbook on page 353.
    const bool continued = node.Text.has_value() && *node.Text == u"\\cfrac"_u16;
    if (continued) {
        const double h_strut = 8.5 / metrics.PtPerEm;
        const double d_strut = 3.5 / metrics.PtPerEm;
        if (numerator->Height < h_strut) {
            numerator->Height = h_strut;
        }
        if (numerator->Depth < d_strut) {
            numerator->Depth = d_strut;
        }
    }
    NodePtr<HtmlSpan> rule;
    double rule_width = 0.0;
    double rule_spacing = 0.0;
    if (node.HasBar) {
        // KaTeX's genfrac builder: an explicit bar size (from `\genfrac` or
        // `\above`) replaces the default rule thickness.
        const bool has_bar_size = node.Text.has_value() &&
                                  (*node.Text == u"\\genfrac"_u16 || *node.Text == u"\\above"_u16);
        if (has_bar_size) {
            const double thickness = CalculateSize(node.Length1.Number, node.Length1.Unit, options);
            rule = MakeLineSpan("frac-line", options, thickness);
        } else {
            rule = MakeLineSpan("frac-line", options);
        }
        rule_width = rule->Height;
        rule_spacing = rule->Height;
    } else {
        rule_spacing = metrics.DefaultRuleThickness;
    }

    // Rule 15(b)
    double num_shift = 0.0;
    double clearance = 0.0;
    double denom_shift = 0.0;
    if (style.Size == 0) {
        num_shift = metrics.Num1;
        clearance = rule_width > 0.0 ? 3 * rule_spacing : 7 * rule_spacing;
        denom_shift = metrics.Denom1;
    } else {
        if (rule_width > 0.0) {
            num_shift = metrics.Num2;
            clearance = rule_spacing;
        } else {
            num_shift = metrics.Num3;
            clearance = 3 * rule_spacing;
        }
        denom_shift = metrics.Denom2;
    }

    VListParams params;
    params.PositionType = VListPositionType::IndividualShift;
    VListElement denominator_element;
    denominator_element.Elem = std::move(denominator);
    VListElement numerator_element;
    numerator_element.Elem = std::move(numerator);

    if (!rule) {
        // Rule 15(c)
        const double candidate_clearance = (num_shift - numerator_element.Elem->Depth) -
                                          (denominator_element.Elem->Height - denom_shift);
        if (candidate_clearance < clearance) {
            num_shift += 0.5 * (clearance - candidate_clearance);
            denom_shift += 0.5 * (clearance - candidate_clearance);
        }
        denominator_element.Shift = denom_shift;
        numerator_element.Shift = -num_shift;
        params.ShiftedElements.push_back(std::move(denominator_element));
        params.ShiftedElements.push_back(std::move(numerator_element));
    } else {
        // Rule 15(d)
        const double axis_height = metrics.AxisHeight;
        if ((num_shift - numerator_element.Elem->Depth) - (axis_height + 0.5 * rule_width) < clearance) {
            num_shift += clearance - ((num_shift - numerator_element.Elem->Depth) -
                                      (axis_height + 0.5 * rule_width));
        }
        if ((axis_height - 0.5 * rule_width) - (denominator_element.Elem->Height - denom_shift) < clearance) {
            denom_shift += clearance - ((axis_height - 0.5 * rule_width) -
                                        (denominator_element.Elem->Height - denom_shift));
        }
        const double mid_shift = -(axis_height - 0.5 * rule_width);
        VListElement rule_element;
        rule_element.Elem = std::move(rule);
        rule_element.Shift = mid_shift;
        denominator_element.Shift = denom_shift;
        numerator_element.Shift = -num_shift;
        params.ShiftedElements.push_back(std::move(denominator_element));
        params.ShiftedElements.push_back(std::move(rule_element));
        params.ShiftedElements.push_back(std::move(numerator_element));
    }

    auto frac = MakeVList(std::move(params), options);

    // \dfrac and \tfrac change the style, so account for the size change.
    const HtmlOptions new_options = options.HavingStyle(style);
    frac->Height *= new_options.SizeMultiplier / options.SizeMultiplier;
    frac->Depth *= new_options.SizeMultiplier / options.SizeMultiplier;

    // Rule 15(e)
    double delim_size = 0.0;
    if (style.Size == 0) {
        delim_size = metrics.Delim1;
    } else if (style.Size == 2) {
        delim_size = options.HavingStyle(Style(4, 2, false)).Metrics().Delim2;
    } else {
        delim_size = metrics.Delim2;
    }

    // `\binom`, `\choose`, `\brace` and friends wrap the fraction in
    // delimiters that are scaled to the fraction's height.
    NodeVector children;
    if (node.LeftDelimiter != 0) {
        children.push_back(MakeCustomSizedDelim(DelimiterName(node.LeftDelimiter), delim_size, true, new_options,
                                                Mode::Math, {"mopen"}));
    } else {
        children.push_back(MakeNullDelimiter(options, {"mopen"}));
    }
    NodeVector mfrac_children;
    mfrac_children.push_back(std::move(frac));
    children.push_back(MakeSpan({"mfrac"}, std::move(mfrac_children)));
    if (continued) {
        // A zero width right delimiter for `\cfrac`.
        children.push_back(MakeSpan({}, {}));
    } else if (node.RightDelimiter != 0) {
        children.push_back(MakeCustomSizedDelim(DelimiterName(node.RightDelimiter), delim_size, true, new_options,
                                                Mode::Math, {"mclose"}));
    } else {
        children.push_back(MakeNullDelimiter(options, {"mclose"}));
    }

    ClassList classes{"mord"};
    new_options.AddSizingClasses(classes, options);
    return MakeSpan(std::move(classes), std::move(children), options);
}

// KaTeX's `sizingGroup` (`src/functions/sizing.ts`): builds the body of a
// sizing or styling command, adds the size resetting classes to the
// individual elements and scales their height and depth.
HtmlNodePtr Builder::SizingGroup(const IndexVector& children, const HtmlOptions& new_options,
                                 const HtmlOptions& base_options) {
    NodeVector inner = BuildExpression(children, new_options, false);
    const double multiplier = new_options.SizeMultiplier / base_options.SizeMultiplier;
    const bool has_sizing = new_options.Size != base_options.Size;
    for (auto& item : inner) {
        // KaTeX handles nested size changes by overriding the `reset-size`
        // class of an element that already carries one.
        size_t pos = item->Classes.Size();
        for (size_t i = 0; i < item->Classes.Size(); ++i) {
            if (item->Classes.At(i) == "katex-sizing") {
                pos = i;
                break;
            }
        }
        if (pos == item->Classes.Size()) {
            if (has_sizing) {
                new_options.AddSizingClasses(item->Classes, base_options);
            }
        } else if (pos + 1 < item->Classes.Size()) {
            const std::string_view reset_size = HtmlOptions::ResetSizeClassName(new_options.Size);
            if (item->Classes.At(pos + 1) == reset_size) {
                item->Classes.SetAt(pos + 1, HtmlOptions::ResetSizeClassName(base_options.Size));
            }
        }
        item->Height *= multiplier;
        item->Depth *= multiplier;
    }
    return MakeFragment(std::move(inner));
}

HtmlNodePtr Builder::BuildStyle(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    Style style = options.CurrentStyle;
    switch (node.Style) {
        case MathStyle::Display: style = Style(0, 0, false); break;
        case MathStyle::Text: style = Style(2, 1, false); break;
        case MathStyle::Script: style = Style(4, 2, false); break;
        case MathStyle::ScriptScript: style = Style(6, 3, false); break;
    }
    const HtmlOptions new_options = options.HavingStyle(style);
    // KaTeX's styling builder: the body is a flat list of nodes, so the
    // group does not add the "mord" span of an ordgroup.  The size resetting
    // classes are added to the elements of the body.
    if (node.ResetFont) {
        // The "hbox" argument type builds a styling group that keeps the
        // argument group, so its "mord" span survives (`src/Parser.ts`).
        IndexVector body{node.FirstChild};
        return SizingGroup(body, new_options, options);
    }
    return SizingGroup(OrdArgument(node.FirstChild), new_options, options);
}

HtmlNodePtr Builder::BuildFont(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    // The semantic tree stores the font as an enum; map it to KaTeX's font
    // class names.
    std::string_view font_class;
    switch (node.Font) {
        case MathFont::MathBold: font_class = "mathbf"; break;
        case MathFont::MathRoman: font_class = "mathrm"; break;
        case MathFont::MathItalic: font_class = "mathit"; break;
        case MathFont::MathBoldItalic: font_class = "boldsymbol"; break;
        case MathFont::MainBoldItalic: font_class = "boldsymbol"; break;
        case MathFont::MathSans:
        case MathFont::MathSansBold:
        case MathFont::MathSansBoldItalic: font_class = "mathsf"; break;
        // KaTeX's font map gives `\mathsfit` its own class, because it uses
        // the italic sans serif font.
        case MathFont::MathSansItalic: font_class = "mathsfit"; break;
        case MathFont::MathTypewriter: font_class = "mathtt"; break;
        case MathFont::AMS: font_class = "mathbb"; break;
        case MathFont::Caligraphic: font_class = "mathcal"; break;
        case MathFont::Fraktur: font_class = "mathfrak"; break;
        case MathFont::Script: font_class = "mathscr"; break;
        case MathFont::MathNormal: font_class = "mathnormal"; break;
        default: break;
    }
    const HtmlOptions new_options = options.WithFont(font_class);
    // KaTeX's `normalizeArgument` drops the group wrapper when the argument
    // holds a single element.
    IndexVector children = Children(node.FirstChild);
    if (children.size() == 1) {
        return BuildGroup(children.front(), new_options, &options);
    }
    return BuildGroup(node.FirstChild, new_options, &options);
}

HtmlNodePtr Builder::BuildText(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    std::string_view family;
    std::string_view weight;
    std::string_view shape;
    if (node.Text.has_value()) {
        const std::string label(ToUtf8(*node.Text));
        if (same_text(label, "\\textrm") || same_text(label, "\\textnormal")) {
            family = "textrm";
        } else if (same_text(label, "\\textsf")) {
            family = "textsf";
        } else if (same_text(label, "\\texttt")) {
            family = "texttt";
        } else if (same_text(label, "\\textbf")) {
            weight = "textbf";
        } else if (same_text(label, "\\textmd")) {
            weight = "textmd";
        } else if (same_text(label, "\\textit")) {
            shape = "textit";
        } else if (same_text(label, "\\textup")) {
            shape = "textup";
        } else if (same_text(label, "\\emph")) {
            shape = options.FontShape == "textit" ? "textup" : "textit";
        }
    }
    HtmlOptions new_options = options;
    if (!family.empty()) {
        new_options = new_options.WithTextFontFamily(family);
    } else if (!weight.empty()) {
        new_options = new_options.WithTextFontWeight(weight);
    } else if (!shape.empty()) {
        new_options = new_options.WithTextFontShape(shape);
    }
    // KaTeX's `text` builder runs `html.buildExpression` over the body of the
    // argument (`src/functions/text.ts`).  Every character stays its own
    // symbol, so that KaTeX's `tryCombineChars` can merge the ones that share
    // the same classes, while characters with a different font or a
    // mathematical alphanumeric symbol keep their own span.
    IndexVector body = Children(node.FirstChild);
    NodeVector inner = BuildExpression(body, new_options, true);
    return MakeSpan({"mord", "text"}, std::move(inner), new_options);
}

HtmlNodePtr Builder::BuildColor(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    // The colour is kept in a string of its own: the options hold a view of
    // it, and the string has to outlive the build of the group.
    std::string color;
    if (node.Text2.has_value()) {
        color = ToUtf8(*node.Text2);
    }
    const HtmlOptions new_options = options.WithColor(color);
    // KaTeX's `color` builder uses `ordargument`, so the body of the command
    // is the content of its ordgroup.
    IndexVector children = OrdArgument(node.FirstChild);
    NodeVector inner = BuildExpression(children, new_options, false);
    return MakeFragment(std::move(inner));
}

HtmlNodePtr Builder::BuildClass(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    std::string label = node.Text.has_value() ? ToUtf8(*node.Text) : std::string();
    if (same_text(label, "\\boldsymbol") || same_text(label, "\\bm")) {
        // KaTeX's `\boldsymbol` is an mclass group that wraps a font group,
        // so the body keeps the "mord" span of its ordgroup.
        const HtmlOptions new_options = options.WithFont("boldsymbol");
        NodeVector children;
        children.push_back(BuildOrdGroup(node.FirstChild, new_options));
        return MakeSpan({AtomClass(node.Atom)}, std::move(children), options);
    }
    if (same_text(label, "\\overset") || same_text(label, "\\underset") || same_text(label, "\\stackrel")) {
        // KaTeX's mclass builder for the stacked operators: the body is a
        // script group that is wrapped in a span carrying the math class.
        NodeVector children;
        children.push_back(BuildGroup(node.FirstChild, options, nullptr));
        const std::string_view mclass = same_text(label, "\\stackrel") ? "mrel" : AtomClass(node.Atom);
        return MakeSpan({mclass}, std::move(children), options);
    }
    if (same_text(label, "\\mathord") || same_text(label, "\\mathbin") || same_text(label, "\\mathrel") ||
        same_text(label, "\\mathopen") || same_text(label, "\\mathclose") || same_text(label, "\\mathpunct") ||
        same_text(label, "\\mathinner")) {
        // KaTeX's mclass builder: the body is built as an expression and
        // wrapped in a span carrying the math class.
        IndexVector children = OrdArgument(node.FirstChild);
        NodeVector inner = BuildExpression(children, options, true);
        return MakeSpan({AtomClass(node.Atom)}, std::move(inner), options);
    }
    if (same_text(label, "\\not")) {
        IndexVector children = Children(node.FirstChild);
        NodeVector inner = BuildExpression(children, options, false);
        NodeVector children_out;
        for (auto& child : inner) {
            children_out.push_back(std::move(child));
        }
        NodeVector with_slash;
        with_slash.push_back(MakeSpan({"mrel", "rlap"}, std::move(children_out)));
        with_slash.push_back(MakeSpan({"mrel", "llap"}, {}));
        return MakeSpan({"mord", "mrel"}, std::move(with_slash));
    }
    IndexVector children = Children(node.FirstChild);
    NodeVector inner = BuildExpression(children, options.WithPhantom(), false);
    NodeVector children_out;
    for (auto& child : inner) {
        children_out.push_back(std::move(child));
    }
    const std::string_view mclass = AtomClass(node.Atom);
    return MakeSpan({mclass}, std::move(children_out));
}

HtmlNodePtr Builder::BuildBox(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    std::string label = node.Text.has_value() ? ToUtf8(*node.Text) : std::string();
    if (same_text(label, "\\phantom")) {
        // KaTeX's phantom builder: the body is flattened and rendered with a
        // transparent colour.
        IndexVector children = OrdArgument(node.FirstChild);
        NodeVector inner = BuildExpression(children, options.WithPhantom(), false);
        return MakeFragment(std::move(inner));
    }
    if (same_text(label, "\\vphantom")) {
        // KaTeX's vphantom builder: the body keeps its size but not its
        // width, and it is placed on the baseline.
        NodeVector inner_children;
        inner_children.push_back(BuildOrdGroup(node.FirstChild, options.WithPhantom()));
        HtmlNodePtr inner = MakeSpan({"katex-inner"}, std::move(inner_children));
        NodeVector children;
        children.push_back(std::move(inner));
        children.push_back(MakeSpan({"katex-fix"}, {}));
        return MakeSpan({"mord", "rlap"}, std::move(children), options);
    }
    if (same_text(label, "\\hphantom")) {
        // KaTeX defines `\hphantom` as `\smash{\phantom{#1}}`.
        HtmlNodePtr inner = BuildOrdGroup(node.FirstChild, options.WithPhantom());
        NodeVector inner_children;
        inner_children.push_back(std::move(inner));
        auto smash = MakeSpan({}, std::move(inner_children));
        smash->Height = 0.0;
        smash->Depth = 0.0;
        NodeVector children;
        children.push_back(std::move(smash));
        return MakeSpan({"mord", "katex-smash"}, std::move(children), options);
    }
    if (same_text(label, "\\smash")) {
        // KaTeX's smash builder (`src/functions/smash.ts`).  The optional
        // argument selects which of the height and the depth is smashed; a
        // symmetric smash keeps the group in the inline layout.
        bool smash_height = true;
        bool smash_depth = true;
        if (node.Alignment.has_value()) {
            smash_height = false;
            smash_depth = false;
            for (const char16_t letter : *node.Alignment) {
                if (letter == u't') {
                    smash_height = true;
                } else if (letter == u'b') {
                    smash_depth = true;
                } else {
                    smash_height = false;
                    smash_depth = false;
                    break;
                }
            }
        }
        NodeVector inner_children;
        inner_children.push_back(BuildOrdGroup(node.FirstChild, options));
        auto smash = MakeSpan({}, std::move(inner_children));
        if (!smash_height && !smash_depth) {
            return smash;
        }
        if (smash_height) {
            smash->Height = 0.0;
        }
        if (smash_depth) {
            smash->Depth = 0.0;
        }
        if (smash_height && smash_depth) {
            NodeVector children;
            children.push_back(std::move(smash));
            return MakeSpan({"mord", "katex-smash"}, std::move(children), options);
        }
        // An asymmetric smash has to reset the height and the depth of the
        // children, because the span keeps its HTML line height.
        for (auto& child : smash->Children()) {
            if (smash_height) {
                child->Height = 0.0;
            }
            if (smash_depth) {
                child->Depth = 0.0;
            }
        }
        VListParams params;
        params.PositionType = VListPositionType::FirstBaseline;
        VListElement element;
        element.Elem = std::move(smash);
        params.Elements.push_back(std::move(element));
        auto smashed = MakeVList(std::move(params), options);
        NodeVector children;
        children.push_back(std::move(smashed));
        return MakeSpan({"mord"}, std::move(children), options);
    }
    if (same_text(label, "\\raisebox")) {
        // KaTeX's raisebox builder: the body is shifted by the given amount.
        // The body is the "hbox" argument group, so it is built as a group
        // and keeps the "mord" span of its argument.
        HtmlNodePtr body = BuildGroup(node.FirstChild, options, nullptr);
        VListParams params;
        params.PositionType = VListPositionType::Shift;
        params.PositionData = -CalculateSize(node.Length1.Number, node.Length1.Unit, options);
        VListElement element;
        element.Elem = std::move(body);
        params.Elements.push_back(std::move(element));
        return MakeVList(std::move(params), options);
    }
    if (same_text(label, "\\vcenter")) {
        // KaTeX's vcenter builder: the body is centred on the math axis.
        HtmlNodePtr body = BuildOrdGroup(node.FirstChild, options);
        const double axis_height = options.Metrics().AxisHeight;
        const double dy = 0.5 * ((body->Height - axis_height) - (body->Depth + axis_height));
        VListParams params;
        params.PositionType = VListPositionType::Shift;
        params.PositionData = dy;
        VListElement element;
        element.Elem = std::move(body);
        params.Elements.push_back(std::move(element));
        return MakeVList(std::move(params), options);
    }
    if (same_text(label, "\\reflectbox")) {
        // KaTeX's reflectbox builder.
        NodeVector children;
        children.push_back(BuildGroup(node.FirstChild, options, nullptr));
        return MakeSpan({"mord", "reflectbox"}, std::move(children), options);
    }
    if (same_text(label, "\\mathrlap") || same_text(label, "\\mathllap") || same_text(label, "\\mathclap")) {
        // KaTeX's lap builder (`src/functions/lap.ts`).
        const bool is_clap = same_text(label, "\\mathclap");
        const bool is_right = same_text(label, "\\mathrlap");
        HtmlNodePtr body = BuildOrdGroup(node.FirstChild, options);
        if (is_clap) {
            // The CSS centers a `.clap > .katex-inner > span`, so the body is
            // wrapped in an extra span.
            NodeVector clap_children;
            clap_children.push_back(std::move(body));
            HtmlNodePtr clap = MakeSpan({}, std::move(clap_children));
            NodeVector inner_children;
            inner_children.push_back(std::move(clap));
            body = MakeSpan({"katex-inner"}, std::move(inner_children), options);
        } else {
            NodeVector inner_children;
            inner_children.push_back(std::move(body));
            body = MakeSpan({"katex-inner"}, std::move(inner_children));
        }
        NodeVector lap_children;
        lap_children.push_back(std::move(body));
        lap_children.push_back(MakeSpan({"katex-fix"}, {}));
        HtmlNodePtr lap = MakeSpan({is_right ? "rlap" : (is_clap ? "clap" : "llap")}, std::move(lap_children), options);

        // A strut sets the height of the HTML bounding box, so that a tall
        // argument is not misplaced.
        HtmlNodePtr strut = MakeSpan({"katex-strut"}, {});
        strut->Height = 0.0;
        strut->Depth = 0.0;
        strut->Style.SetEm("height", lap->Height + lap->Depth);
        if (lap->Depth != 0.0) {
            strut->Style.SetEm("vertical-align", -lap->Depth);
        }
        NodeVector& lap_kids = lap->Children();
        lap_kids.insert(lap_kids.begin(), std::move(strut));

        NodeVector thin_children;
        thin_children.push_back(std::move(lap));
        HtmlNodePtr thin = MakeSpan({"katex-thinbox"}, std::move(thin_children), options);
        NodeVector children;
        children.push_back(std::move(thin));
        return MakeSpan({"mord", "katex-vbox"}, std::move(children), options);
    }
    if (same_text(label, "\\hbox")) {
        // KaTeX's `hbox` builder: the body is the argument without its group
        // wrapper, so the result is a plain fragment (`src/functions/hbox.ts`).
        IndexVector children = OrdArgument(node.FirstChild);
        NodeVector inner = BuildExpression(children, options, false);
        return MakeFragment(std::move(inner));
    }
    if (same_text(label, "\\pmb")) {
        // KaTeX's `pmb` builder (`src/functions/pmb.ts`): the body is drawn
        // three times with small offsets, which CSS does with a text shadow.
        IndexVector body = OrdArgument(node.FirstChild);
        NodeVector inner = BuildExpression(body, options, true);
        HtmlNodePtr span = MakeSpan({AtomClass(node.Atom)}, std::move(inner), options);
        span->Style.Set("text-shadow", "0.02em 0.01em 0.04px");
        return span;
    }
    return MakeSpan({}, {});
}

HtmlNodePtr Builder::BuildSizing(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    const HtmlOptions new_options = options.HavingSize(static_cast<int>(node.Value));
    return SizingGroup(Children(node.FirstChild), new_options, options);
}

HtmlNodePtr Builder::BuildChoice(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    int selected = node.FirstChild;
    switch (options.CurrentStyle.Id) {
        case 0: selected = node.FirstChild; break;
        case 2: selected = node.SecondChild; break;
        case 4:
        case 5: selected = node.ThirdChild; break;
        default: selected = node.FourthChild; break;
    }
    if (node.Kind == NodeKind::MathMlChoice) {
        // `\html@mathml{html}{mathml}` keeps the HTML branch in the first slot
        // and the MathML branch in the second one (`src/functions/htmlmathml.ts`).
        selected = node.FirstChild;
    }
    // KaTeX builds the selected branch as an expression, without the group
    // wrapper.
    IndexVector children = Children(selected);
    NodeVector inner = BuildExpression(children, options, false);
    return MakeFragment(std::move(inner));
}

HtmlNodePtr Builder::BuildVerb(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    // KaTeX's `makeVerb` (`src/functions/verb.ts`): `\verb*` replaces every
    // space with an open box, `\verb` with a no-break space.
    const bool starred = node.Value != 0;
    std::u16string content = node.Text.value_or(std::u16string());
    for (char16_t& character : content) {
        if (character == u' ') {
            character = starred ? u'\u2423' : u'\u00A0';
        }
    }
    // `\verb` enters text mode and therefore is sized like `\textstyle`.
    const HtmlOptions new_options = options.HavingStyle(Style(2, 1, false));
    NodeVector body;
    body.reserve(content.size());
    for (const char16_t character : content) {
        // KaTeX renders a tilde through the `\textasciitilde` symbol.
        const std::u16string value = character == u'~' ? std::u16string(u"\\textasciitilde"_u16)
                                                       : std::u16string(1, character);
        body.push_back(MakeSymbol(value, FontId::TypewriterRegular, Mode::Text, new_options, {"mord", "texttt"}));
    }
    ClassList classes{"mord", "text"};
    new_options.AddSizingClasses(classes, options);
    return MakeSpan(std::move(classes), TryCombineChars(std::move(body)), new_options);
}

// KaTeX's `assembleSupSub` for operators with limits
// (`src/functions/utils/assembleSupSub.ts`).
HtmlNodePtr Builder::BuildOperatorLimits(int index, const HtmlOptions& options, int sup_index, int sub_index) {
    HtmlNodePtr base = BuildOperatorBase(index, options);
    double slant = 0.0;
    double base_shift = 0.0;
    // KaTeX's `suppressBaseShift` keeps the base of `\overset` and
    // `\underset` on the axis (`src/functions/mclass.ts`).
    const bool suppress_base_shift = Node(index).Value != 0.0;
    if (!suppress_base_shift && base->Kind() == HtmlNodeKind::Symbol) {
        base_shift = (base->Height - base->Depth) / 2.0 - options.Metrics().AxisHeight;
        slant = static_cast<HtmlSymbol&>(*base).ItalicValue();
    } else if (!suppress_base_shift && IsOiintLabel(text(Node(index).Text))) {
        // `\oiint` and `\oiiint` carry the italic correction of the integral
        // they are built from (`src/functions/op.ts`).
        base_shift = (base->Height - base->Depth) / 2.0 - options.Metrics().AxisHeight;
        slant = base->Italic;
    }
    {
        NodeVector base_children;
        base_children.push_back(std::move(base));
        base = MakeSpan({}, std::move(base_children));
    }

    const Style& style = options.CurrentStyle;
    const GlobalMetrics& metrics = options.Metrics();
    HtmlNodePtr sup;
    double sup_kern = 0.0;
    HtmlNodePtr sub;
    double sub_kern = 0.0;
    if (sup_index >= 0) {
        const HtmlOptions new_options = options.HavingStyle(style.Sup());
        sup = BuildGroup(sup_index, new_options, &options);
        sup_kern = std::max(metrics.BigOpSpacing1, metrics.BigOpSpacing3 - sup->Depth);
    }
    if (sub_index >= 0) {
        const HtmlOptions new_options = options.HavingStyle(style.Sub());
        sub = BuildGroup(sub_index, new_options, &options);
        sub_kern = std::max(metrics.BigOpSpacing2, metrics.BigOpSpacing4 - sub->Height);
    }

    VListParams params;
    if (sup && sub) {
        const double bottom = metrics.BigOpSpacing5 + sub->Height + sub->Depth + sub_kern + base->Depth + base_shift;
        params.PositionType = VListPositionType::Bottom;
        params.PositionData = bottom;
        VListElement kern_one;
        kern_one.IsKern = true;
        kern_one.KernSize = metrics.BigOpSpacing5;
        params.Elements.push_back(std::move(kern_one));
        VListElement sub_element;
        sub_element.Elem = std::move(sub);
        sub_element.MarginLeft = -slant;
        params.Elements.push_back(std::move(sub_element));
        VListElement kern_two;
        kern_two.IsKern = true;
        kern_two.KernSize = sub_kern;
        params.Elements.push_back(std::move(kern_two));
        VListElement base_element;
        base_element.Elem = std::move(base);
        params.Elements.push_back(std::move(base_element));
        VListElement kern_three;
        kern_three.IsKern = true;
        kern_three.KernSize = sup_kern;
        params.Elements.push_back(std::move(kern_three));
        VListElement sup_element;
        sup_element.Elem = std::move(sup);
        sup_element.MarginLeft = slant;
        params.Elements.push_back(std::move(sup_element));
        VListElement kern_four;
        kern_four.IsKern = true;
        kern_four.KernSize = metrics.BigOpSpacing5;
        params.Elements.push_back(std::move(kern_four));
    } else if (sub) {
        const double top = base->Height - base_shift;
        params.PositionType = VListPositionType::Top;
        params.PositionData = top;
        VListElement kern_one;
        kern_one.IsKern = true;
        kern_one.KernSize = metrics.BigOpSpacing5;
        params.Elements.push_back(std::move(kern_one));
        VListElement sub_element;
        sub_element.Elem = std::move(sub);
        sub_element.MarginLeft = -slant;
        params.Elements.push_back(std::move(sub_element));
        VListElement kern_two;
        kern_two.IsKern = true;
        kern_two.KernSize = sub_kern;
        params.Elements.push_back(std::move(kern_two));
        VListElement base_element;
        base_element.Elem = std::move(base);
        params.Elements.push_back(std::move(base_element));
    } else if (sup) {
        const double bottom = base->Depth + base_shift;
        params.PositionType = VListPositionType::Bottom;
        params.PositionData = bottom;
        VListElement base_element;
        base_element.Elem = std::move(base);
        params.Elements.push_back(std::move(base_element));
        VListElement kern_one;
        kern_one.IsKern = true;
        kern_one.KernSize = sup_kern;
        params.Elements.push_back(std::move(kern_one));
        VListElement sup_element;
        sup_element.Elem = std::move(sup);
        sup_element.MarginLeft = slant;
        params.Elements.push_back(std::move(sup_element));
        VListElement kern_two;
        kern_two.IsKern = true;
        kern_two.KernSize = metrics.BigOpSpacing5;
        params.Elements.push_back(std::move(kern_two));
    } else {
        return base;
    }

    auto final_group = MakeVList(std::move(params), options);
    NodeVector parts;
    parts.push_back(std::move(final_group));
    return MakeSpan({"mop", "op-limits"}, std::move(parts), options);
}

// KaTeX's square root builder (`src/functions/sqrt.ts`).
HtmlNodePtr Builder::BuildRadical(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    // Rule 11: build the inner group first.
    HtmlNodePtr inner = BuildGroup(node.FirstChild, options.HavingCrampedStyle(), nullptr);
    if (inner->Height == 0.0) {
        inner->Height = options.Metrics().XHeight;
    }
    inner = WrapFragment(std::move(inner), options);

    const GlobalMetrics& metrics = options.Metrics();
    const double theta = metrics.DefaultRuleThickness;
    double phi = theta;
    if (options.CurrentStyle.Id < 2) {
        phi = metrics.XHeight;
    }
    double line_clearance = theta + phi / 4.0;
    const double min_delimiter_height = inner->Height + inner->Depth + line_clearance + theta;

    SqrtImage image = MakeSqrtImage(min_delimiter_height, options);
    const double delim_depth = image.Span->Height - image.RuleWidth;
    if (delim_depth > inner->Height + inner->Depth + line_clearance) {
        line_clearance = (line_clearance + delim_depth - inner->Height - inner->Depth) / 2.0;
    }
    const double img_shift = image.Span->Height - inner->Height - line_clearance - image.RuleWidth;

    inner->Style.SetEm("padding-left", image.AdvanceWidth);

    VListParams params;
    params.PositionType = VListPositionType::FirstBaseline;
    VListElement inner_element;
    inner_element.Elem = std::move(inner);
    inner_element.WrapperClasses = {"svg-align"};
    params.Elements.push_back(std::move(inner_element));
    VListElement kern_one;
    kern_one.IsKern = true;
    kern_one.KernSize = -(params.Elements.front().Elem->Height + img_shift);
    params.Elements.push_back(std::move(kern_one));
    VListElement image_element;
    image_element.Elem = std::move(image.Span);
    params.Elements.push_back(std::move(image_element));
    VListElement kern_two;
    kern_two.IsKern = true;
    kern_two.KernSize = image.RuleWidth;
    params.Elements.push_back(std::move(kern_two));

    auto body = MakeVList(std::move(params), options);

    if (node.SecondChild < 0) {
        NodeVector children;
        children.push_back(std::move(body));
        return MakeSpan({"mord", "sqrt"}, std::move(children), options);
    }
    // The root index is always in scriptscript style.
    const HtmlOptions new_options = options.HavingStyle(Style(6, 3, true));
    HtmlNodePtr root = BuildGroup(node.SecondChild, new_options, &options);
    const double to_shift = 0.6 * (body->Height - body->Depth);
    VListParams root_params;
    root_params.PositionType = VListPositionType::Shift;
    root_params.PositionData = -to_shift;
    VListElement root_element;
    root_element.Elem = std::move(root);
    root_params.Elements.push_back(std::move(root_element));
    auto root_vlist = MakeVList(std::move(root_params), options);
    NodeVector root_children;
    root_children.push_back(std::move(root_vlist));
    auto root_wrap = MakeSpan({"katex-root"}, std::move(root_children));
    NodeVector children;
    children.push_back(std::move(root_wrap));
    children.push_back(std::move(body));
    return MakeSpan({"mord", "sqrt"}, std::move(children), options);
}

// KaTeX's `leftright` builder (`src/functions/delimsizing.ts`): the group
// between `\left` and `\right`.
HtmlNodePtr Builder::BuildDelimited(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    const Mode mode = node.TextMode ? Mode::Text : Mode::Math;

    // Build the inner expression.  KaTeX surrounds the body with "mopen" and
    // "mclose" so that the spacing at the edges of the body matches the
    // delimiters.  The middle delimiters of an enclosing group are kept aside
    // while this group is built.
    const IndexVector body = Children(node.FirstChild);
    std::vector<std::pair<const HtmlNode*, std::u16string>> outer_middles;
    outer_middles.swap(middle_delimiters_);
    NodeVector inner = BuildExpression(body, options, true, "mopen", "mclose");

    // Calculate the height and depth of the body.  Middle delimiters are
    // excluded: they are resized below.
    double inner_height = 0.0;
    double inner_depth = 0.0;
    for (const auto& child : inner) {
        bool is_middle = false;
        for (const auto& middle : middle_delimiters_) {
            if (middle.first == child.get()) {
                is_middle = true;
                break;
            }
        }
        if (is_middle) {
            continue;
        }
        inner_height = std::max(child->Height, inner_height);
        inner_depth = std::max(child->Depth, inner_depth);
    }

    // The size of delimiters is the same, regardless of what style we are in.
    // Thus, to correctly calculate the size of the delimiter we need around a
    // group, we scale down the inner size based on the size.
    inner_height *= options.SizeMultiplier;
    inner_depth *= options.SizeMultiplier;

    // Resize the middle delimiters now that the height of the body is known.
    for (auto& child : inner) {
        for (const auto& middle : middle_delimiters_) {
            if (middle.first == child.get()) {
                child = MakeLeftRightDelim(middle.second, inner_height, inner_depth, options, mode, {});
                break;
            }
        }
    }

    // Empty delimiters in \left and \right make null delimiter spaces.
    HtmlNodePtr left_delim;
    if (node.LeftDelimiter == 0) {
        left_delim = MakeNullDelimiter(options, {"mopen"});
    } else {
        left_delim = MakeLeftRightDelim(DelimiterName(node.LeftDelimiter), inner_height, inner_depth, options,
                                         mode, {"mopen"});
    }
    // Add it to the beginning of the expression.
    inner.insert(inner.begin(), std::move(left_delim));

    // Same for the right delimiter.
    HtmlNodePtr right_delim;
    if (node.RightDelimiter == 0) {
        right_delim = MakeNullDelimiter(options, {"mclose"});
    } else {
        right_delim = MakeLeftRightDelim(DelimiterName(node.RightDelimiter), inner_height, inner_depth, options,
                                          mode, {"mclose"});
    }
    // Add it to the end of the expression.
    inner.push_back(std::move(right_delim));

    // Restore the middle delimiters of the enclosing group.
    middle_delimiters_.swap(outer_middles);

    return MakeSpan({"minner"}, std::move(inner), options);
}

// KaTeX's `delimsizing` builder (`src/functions/delimsizing.ts`), used by
// `\big`, `\middle` and friends.
HtmlNodePtr Builder::BuildDelimiter(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    const Mode mode = node.TextMode ? Mode::Text : Mode::Math;
    const std::u16string delim = DelimiterName(node.CodePoint);
    const std::string_view mclass = AtomClass(node.Atom);
    if (node.CodePoint == 0) {
        // Empty delimiters still count as elements, even though they do not
        // show anything.
        if (node.Value == 0) {
            return MakeNullDelimiter(options, {});
        }
        return MakeSpan({mclass}, {});
    }
    if (node.Value == 0) {
        // A `\middle` delimiter is sized by the surrounding `\left`/`\right`
        // group, so it is built at size 1 first and resized later.
        auto span = MakeSizedDelim(delim, 1, options, mode, {});
        middle_delimiters_.emplace_back(span.get(), delim);
        return span;
    }
    return MakeSizedDelim(delim, node.Value, options, mode, {mclass});
}

// KaTeX's operator builder (`src/functions/op.ts`).
HtmlNodePtr Builder::BuildOperatorBase(int index, const HtmlOptions& options) {
    const SemanticNode& node = Node(index);
    const std::string label = node.Text.has_value() ? ToUtf8(*node.Text) : std::string();
    HtmlNodePtr base;
    if (node.CodePoint > 0) {
        // A symbol operator such as the sum or integral sign.
        const bool large = options.CurrentStyle.Size == 0;
        const FontId font = large ? FontId::Size2Regular : FontId::Size1Regular;
        std::u16string name(1, static_cast<char16_t>(node.CodePoint));
        // `\oiint` and `\oiiint` have no glyph in the KaTeX fonts.  KaTeX
        // draws the plain integral and overlays the oval on top of it
        // (`src/functions/op.ts`).
        const bool is_oiint = IsOiintLabel(text(node.Text));
        if (is_oiint) {
            name.assign(1, static_cast<char16_t>(text(node.Text) == u"\\oiint"_u16 ? 0x222c : 0x222d));
        }
        base = MakeSymbol(name, font, Mode::Math, options,
                          {"mop", "op-symbol", large ? "large-op" : "small-op"});
        if (is_oiint) {
            // The italic correction is taken from the original symbol and
            // carried over to the vlist that wraps it.
            const double symbol_italic = static_cast<HtmlSymbol&>(*base).ItalicValue();
            const std::u16string oval_name =
                text(node.Text) == u"\\oiint"_u16 ? (large ? u"oiintSize2" : u"oiintSize1")
                                             : (large ? u"oiiintSize2" : u"oiiintSize1");
            const double oval_width = text(node.Text) == u"\\oiint"_u16 ? (large ? 1.472 : 0.957)
                                                                     : (large ? 1.98 : 1.304);
            const double oval_height = large ? 0.659 : 0.499;
            auto oval = MakeStaticSvg(oval_name, oval_width, oval_height, options);
            VListParams params;
            params.PositionType = VListPositionType::IndividualShift;
            VListElement symbol_element;
            symbol_element.Elem = std::move(base);
            symbol_element.Shift = 0.0;
            VListElement oval_element;
            oval_element.Elem = std::move(oval);
            oval_element.Shift = large ? 0.08 : 0.0;
            params.ShiftedElements.push_back(std::move(symbol_element));
            params.ShiftedElements.push_back(std::move(oval_element));
            auto vlist = MakeVList(std::move(params), options);
            // KaTeX prepends the "mop" class to the vlist wrapper.
            vlist->Classes.Prepend("mop");
            vlist->Italic = symbol_italic;
            base = std::move(vlist);
        }
    } else if (same_text(label, "\\operatorname")) {
        // KaTeX's `operatorname` builder renders the body of the operator with
        // \mathrm and wraps the result in a "mop" span.  The body keeps its
        // own builders, so a single operator such as `\max` stays an
        // operator.
        IndexVector body;
        if (node.FirstChild >= 0 && Node(node.FirstChild).Kind == NodeKind::Row) {
            body = Children(node.FirstChild);
        } else if (node.FirstChild >= 0) {
            body.push_back(node.FirstChild);
        }
        NodeVector inner = BuildExpression(body, options.WithFont("mathrm"), true);
        base = MakeSpan({"mop"}, std::move(inner), options);
    } else if (same_text(label, "\\mathop")) {
        // KaTeX's `\mathop` builder: a single symbol becomes the operator
        // itself, anything else is wrapped in a "mop" span.
        IndexVector body;
        if (node.FirstChild >= 0 && Node(node.FirstChild).Kind == NodeKind::Row) {
            body = Children(node.FirstChild);
        } else if (node.FirstChild >= 0) {
            body.push_back(node.FirstChild);
        }
        NodeVector inner = BuildExpression(body, options, true);
        if (inner.size() == 1 && inner.front()->Kind() == HtmlNodeKind::Symbol) {
            base = std::move(inner.front());
            if (!base->Classes.Empty()) {
                base->Classes.SetFront("mop");
            }
        } else {
            base = MakeSpan({"mop"}, std::move(inner), options);
        }
    } else {
        // A named text operator such as \exp or \lim.  KaTeX builds one
        // symbol per character and wraps them in a "mop" span, so the
        // operator as a whole is never shifted onto the axis.
        NodeVector output;
        for (size_t i = 1; i < label.size(); ++i) {
            const std::u16string ch(1, static_cast<char16_t>(label[i]));
            output.push_back(MathSym(ch, Mode::Math, options, {}));
        }
        base = MakeSpan({"mop"}, std::move(output), options);
    }
    return base;
}

HtmlNodePtr Builder::BuildOperator(int index, const HtmlOptions& options) {
    HtmlNodePtr base = BuildOperatorBase(index, options);
    // Rule 13: shift a single symbol so that its centre lies on the axis.
    // Operators with limits are shifted by the vlist instead.
    if (base->Kind() == HtmlNodeKind::Symbol) {
        const double base_shift =
            (base->Height - base->Depth) / 2.0 - options.Metrics().AxisHeight;
        if (base_shift != 0.0) {
            base->Style.Set("position", "relative");
            base->Style.SetEm("top", base_shift);
        }
    } else if (IsOiintLabel(text(Node(index).Text))) {
        // `\oiint` and `\oiiint` are drawn as a vlist, but KaTeX still shifts
        // them onto the axis (`src/functions/op.ts`).
        const double base_shift =
            (base->Height - base->Depth) / 2.0 - options.Metrics().AxisHeight;
        if (base_shift != 0.0) {
            base->Style.Set("position", "relative");
            base->Style.SetEm("top", base_shift);
        }
    }
    return base;
}

void Builder::Build(MarkupBuffer& out) {
    HtmlOptions options;
    options.CurrentStyle = options_.DisplayMode ? Style(0, 0, false) : Style(2, 1, false);
    options.MaxSize = options_.MaxSize;
    options.MinRuleThickness = options_.MinRuleThickness;

    // KaTeX's `buildHTML` (`src/buildHTML.ts`) unwraps the "tag" node that
    // wraps an equation with a tag: the body becomes the expression and the
    // tag is rendered next to it.
    int body_root = root_;
    int tag = -1;
    if (!semantic_.empty() && root_ >= 0 && Node(root_).Kind == NodeKind::Tag) {
        body_root = Node(root_).FirstChild;
        tag = Node(root_).SecondChild;
    }

    NodeVector expression;
    if (!semantic_.empty() && body_root >= 0) {
        IndexVector nodes = Children(body_root);
        expression = BuildExpression(nodes, options, true, "leftmost", "rightmost", true);
    }

    // An environment with automatic equation numbers (e.g. `{gather}`) puts
    // the number into its own column; KaTeX moves that column out of the
    // expression and next to it.
    HtmlNodePtr eqn_num;
    if (expression.size() == 2 && expression[1]->HasClass("katex-tag")) {
        eqn_num = std::move(expression[1]);
        expression.pop_back();
    }

    NodeVector children;
    NodeVector parts;
    for (size_t i = 0; i < expression.size(); ++i) {
        // The node is kept alive in `current` because a line break is used
        // twice: it ends the current chunk and starts the next one.
        HtmlNodePtr current = std::move(expression[i]);
        const HtmlNode& node = *current;
        if (node.HasClass("mbin") || node.HasClass("mrel") || node.HasClass("allowbreak")) {
            parts.push_back(std::move(current));
            bool nobreak = false;
            while (i + 1 < expression.size() && expression[i + 1]->HasClass("mspace") &&
                   !expression[i + 1]->HasClass("katex-newline")) {
                ++i;
                parts.push_back(std::move(expression[i]));
                if (parts.back()->HasClass("nobreak")) {
                    nobreak = true;
                }
            }
            if (!nobreak) {
                children.push_back(BuildUnbreakable(parts, options));
                parts.clear();
            }
        } else if (node.HasClass("katex-newline")) {
            // The line break itself is not part of the chunk.
            if (!parts.empty()) {
                children.push_back(BuildUnbreakable(parts, options));
                parts.clear();
            }
            children.push_back(std::move(current));
        } else {
            parts.push_back(std::move(current));
        }
    }
    if (!parts.empty()) {
        children.push_back(BuildUnbreakable(parts, options));
    }

    // KaTeX renders the tag of an equation as a final child of the
    // `katex-html` node and stretches its strut to the height of the whole
    // node.
    HtmlNode* tag_strut = nullptr;
    if (tag >= 0) {
        NodeVector tag_expression = BuildExpression({tag}, options, true);
        HtmlNodePtr tag_child = BuildUnbreakable(tag_expression, options);
        tag_child->Classes = "katex-tag";
        if (!tag_child->Children().empty() &&
            tag_child->Children().front()->Kind() == HtmlNodeKind::Span) {
            tag_strut = tag_child->Children().front().get();
        }
        children.push_back(std::move(tag_child));
    } else if (eqn_num) {
        children.push_back(std::move(eqn_num));
    }

    auto html_node = MakeNode<HtmlSpan>(ClassList{"katex-html"});
    html_node->Children() = std::move(children);
    SizeElementFromChildren(*html_node);
    html_node->SetAttribute("aria-hidden", "true");
    if (tag_strut != nullptr) {
        tag_strut->Style.SetEm("height", html_node->Height + html_node->Depth);
        if (html_node->Depth != 0.0) {
            tag_strut->Style.SetEm("vertical-align", -html_node->Depth);
        }
    }

    // The markup is written straight into the caller's buffer so that the
    // string does not have to grow from empty on every render.
    html_node->WriteMarkup(out);
}

void BuildHtmlMarkup(std::span<const SemanticNode> semantic, int root, const MathOptions& options, MarkupBuffer& out) {
    // The nodes of the previous render are gone; their arena is reused.
    ResetNodeArena();
    Builder builder(semantic, root, options);
    builder.Build(out);
}

}  // namespace velocitex::html
