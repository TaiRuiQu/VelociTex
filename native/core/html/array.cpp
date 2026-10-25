// KaTeX-compatible rendering of matrix and array environments.
//
// Port of KaTeX 0.19.0 `src/environments/array.ts` (the `array` HTML
// builder) together with the `leftright` wrapper that the matrix and cases
// environments use for their surrounding delimiters (KaTeX 0.19.0
// `src/functions/delimsizing.ts`).
#include "array.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "build_common.hpp"
#include "delimiter.hpp"
#include "jsnum.hpp"

namespace velocitex::html {
namespace {

// KaTeX's `AlignSpec`: the description of a single column of an array.
struct AlignSpec {
    bool IsSeparator{false};
    char16_t Separator{0};   // '|' (solid rule) or ':' (dashed rule)
    std::string Align{"c"};  // "c", "l" or "r"
    bool HasPregap{false};
    double Pregap{0.0};
    bool HasPostgap{false};
    double Postgap{0.0};
};

// KaTeX's `Outrow`: the rendered cells of one row plus the row's metrics.
struct OutRow {
    NodeVector Cells;
    double Height{0.0};
    double Depth{0.0};
    double Pos{0.0};
};

// KaTeX's `hlines` entry: the position of one horizontal rule.
struct HLine {
    double Pos{0.0};
    bool IsDashed{false};
};

// The environment families that share one set of layout parameters.
enum class EnvFamily {
    Matrix,      // matrix, pmatrix, bmatrix, Bmatrix, vmatrix, Vmatrix
    Array,       // array, darray
    SubArray,    // subarray
    SmallMatrix, // smallmatrix
    Cases,       // cases, dcases, rcases, drcases
    Aligned,     // aligned, alignedat, align, align*, alignat, alignat*, split
    Gathered,    // gathered, gather, gather*
    Equation,    // equation, equation*
    CD,          // CD
};

// The layout parameters KaTeX derives from the environment name.
struct ArrayParams {
    EnvFamily Family{EnvFamily::Matrix};
    std::u16string Name;       // the environment name without the mathtools star
    double ArrayStretch{1.0};  // KaTeX's `arraystretch`
    bool HskipBeforeAndAfter{false};
    bool AddJot{false};
    bool EmptySingleRow{false};
    bool Small{false};  // KaTeX's `colSeparationType === "small"`
    bool CD{false};     // KaTeX's `colSeparationType === "CD"`
    bool AlignedAt{false};  // the column pair count is given explicitly
};

// Removes the mathtools star suffix from an environment name.
std::u16string TrimStar(std::u16string_view name) {
    if (!name.empty() && name.back() == u'*') {
        name.remove_suffix(1);
    }
    return std::u16string(name);
}

// Classifies an environment name.
EnvFamily FamilyOf(std::u16string_view name) {
    if (name == u"array"_u16 || name == u"darray"_u16) return EnvFamily::Array;
    if (name == u"subarray"_u16) return EnvFamily::SubArray;
    if (name == u"smallmatrix"_u16) return EnvFamily::SmallMatrix;
    if (name == u"cases"_u16 || name == u"dcases"_u16 || name == u"rcases"_u16 || name == u"drcases"_u16) {
        return EnvFamily::Cases;
    }
    if (name == u"aligned"_u16 || name == u"alignedat"_u16 || name == u"align"_u16 || name == u"align*"_u16 ||
        name == u"alignat"_u16 || name == u"alignat*"_u16 || name == u"split"_u16) {
        return EnvFamily::Aligned;
    }
    if (name == u"gathered"_u16 || name == u"gather"_u16 || name == u"gather*"_u16) return EnvFamily::Gathered;
    if (name == u"equation"_u16 || name == u"equation*"_u16) return EnvFamily::Equation;
    if (name == u"CD"_u16) return EnvFamily::CD;
    return EnvFamily::Matrix;
}

// The style KaTeX uses for the cells of an environment (`dCellStyle`).  The
// parser stores the style of every cell in a Style node, so this is only the
// fallback for cells without such a node.
Style CellStyleOf(std::u16string_view name) {
    // KaTeX's `dCellStyle` uses the display style for the environments whose
    // name starts with a "d"; the amsmath environments always use it.
    if (name == u"smallmatrix"_u16 || name == u"subarray"_u16) return Style(4, 2, false);
    if (name == u"aligned"_u16 || name == u"alignedat"_u16 || name == u"align"_u16 || name == u"align*"_u16 ||
        name == u"alignat"_u16 || name == u"alignat*"_u16 || name == u"split"_u16 || name == u"gathered"_u16 ||
        name == u"gather"_u16 || name == u"gather*"_u16 || name == u"equation"_u16 || name == u"equation*"_u16 ||
        name == u"CD"_u16 || name == u"darray"_u16 || name == u"dcases"_u16 || name == u"drcases"_u16) {
        return Style(0, 0, false);
    }
    return Style(2, 1, false);
}

// The TeX style that belongs to a semantic style node.
Style StyleOf(MathStyle style) {
    switch (style) {
        case MathStyle::Display: return Style(0, 0, false);
        case MathStyle::Text: return Style(2, 1, false);
        case MathStyle::Script: return Style(4, 2, false);
        case MathStyle::ScriptScript: return Style(6, 3, false);
    }
    return Style(2, 1, false);
}

// KaTeX's `makeNullDelimiter`: the empty delimiter that `\left.` produces.
NodePtr<HtmlSpan> MakeArrayNullDelimiter(const HtmlOptions& options, ClassList&& classes) {
    classes.Add("nulldelimiter");
    options.AddBaseSizingClasses(classes);
    return MakeSpan(std::move(classes), {}, options);
}

// Reads the column specification of the `array` and `subarray` environments.
std::vector<AlignSpec> ParseColumnSpec(std::u16string_view spec) {
    std::vector<AlignSpec> cols;
    for (char16_t c : spec) {
        AlignSpec align;
        if (c == u'l' || c == u'c' || c == u'r') {
            align.Align = c == u'l' ? "l" : (c == u'r' ? "r" : "c");
        } else if (c == u'|' || c == u':') {
            align.IsSeparator = true;
            align.Separator = c;
        } else {
            continue;
        }
        cols.push_back(align);
    }
    return cols;
}

// The column specifications of the `cases` family.
std::vector<AlignSpec> CasesColumnSpec() {
    std::vector<AlignSpec> cols(2);
    cols[0].Align = "l";
    cols[0].HasPregap = true;
    cols[0].Pregap = 0.0;
    cols[0].HasPostgap = true;
    cols[0].Postgap = 1.0;  // 1em quad
    cols[1].Align = "l";
    cols[1].HasPregap = true;
    cols[1].Pregap = 0.0;
    cols[1].HasPostgap = true;
    cols[1].Postgap = 0.0;
    return cols;
}

// The column specifications of the amsmath alignment environments.  KaTeX's
// `alignedHandler` alternates right and left aligned columns and adds one
// `\qquad` in front of every second column pair of an `aligned` environment.
std::vector<AlignSpec> AlignedColumnSpec(int num_cols, bool is_aligned) {
    std::vector<AlignSpec> cols(static_cast<size_t>(std::max(num_cols, 0)));
    for (int i = 0; i < num_cols; ++i) {
        AlignSpec& align = cols[static_cast<size_t>(i)];
        align.Align = (i % 2 == 1) ? "l" : "r";
        align.HasPregap = true;
        align.Pregap = (i % 2 == 0 && i > 0 && is_aligned) ? 1.0 : 0.0;
        align.HasPostgap = true;
        align.Postgap = 0.0;
    }
    return cols;
}

// ---------------------------------------------------------------------------
// KaTeX's `cdlabel` and `cdlabelparent` builders (`src/environments/cd.ts`).
//
// The {CD} environment places the labels of a vertical arrow next to it, which
// needs its own builders.  The C++ core keeps them in Box nodes, so they are
// rendered here, where the cells of the environment are built.
// ---------------------------------------------------------------------------

// The command that marks a Box node as one of the CD builders.
bool IsCdNode(const ModuleContext& context, int index, std::u16string_view* label) {
    if (index < 0) {
        return false;
    }
    const SemanticNode& node = context.Node(index);
    if (node.Kind != NodeKind::Box || !node.Text.has_value()) {
        return false;
    }
    if (*node.Text == u"cdleft"_u16 || *node.Text == u"cdright"_u16 || *node.Text == u"cdparent"_u16) {
        *label = *node.Text;
        return true;
    }
    return false;
}

// KaTeX's `buildGroup` with a base options, i.e. the wrapper that resets the
// size when the group was built in a smaller size (`src/buildHTML.ts`).
HtmlNodePtr BuildGroupWithBase(const ModuleContext& context, int index, const HtmlOptions& options,
                               const HtmlOptions& base_options) {
    HtmlNodePtr result = context.build_group(index, options);
    if (index < 0 || options.Size == base_options.Size) {
        return result;
    }
    NodeVector children;
    children.push_back(std::move(result));
    ClassList sizing_classes;
    options.AddSizingClasses(sizing_classes, base_options);
    auto wrapper = MakeSpan(std::move(sizing_classes), std::move(children), options);
    const double multiplier = options.SizeMultiplier / base_options.SizeMultiplier;
    wrapper->Height *= multiplier;
    wrapper->Depth *= multiplier;
    return wrapper;
}

// KaTeX's `cdlabel` builder: the label of a vertical arrow is rendered in the
// superscript style and pinned to the arrow with a `bottom` offset.
HtmlNodePtr BuildCdLabel(const ModuleContext& context, int index, const HtmlOptions& options) {
    const SemanticNode& node = context.Node(index);
    const HtmlOptions new_options = options.HavingStyle(options.CurrentStyle.Sup());
    HtmlNodePtr label = BuildGroupWithBase(context, node.FirstChild, new_options, options);
    label = WrapFragment(std::move(label), options);
    label->AddClass(text(node.Text) == u"cdleft"_u16 ? "cd-label-left" : "cd-label-right");
    label->Style.SetEm("bottom", 0.8 - label->Depth);
    // Zero out the label's height and depth, so that the vertical alignment of
    // the arrow is set by the arrow's height and not by the label's.
    label->Height = 0.0;
    label->Depth = 0.0;
    return label;
}

// KaTeX's `cdlabelparent` builder: the vertical arrow together with its
// labels.
HtmlNodePtr BuildCdParent(const ModuleContext& context, int index, const HtmlOptions& options);

// Builds a node of a {CD} cell, using the CD builders for the arrow cells.
HtmlNodePtr BuildCdNode(const ModuleContext& context, int index, const HtmlOptions& options) {
    std::u16string_view label;
    if (IsCdNode(context, index, &label)) {
        if (label == u"cdparent"_u16) {
            return BuildCdParent(context, index, options);
        }
        return BuildCdLabel(context, index, options);
    }
    return context.build_group(index, options);
}

HtmlNodePtr BuildCdParent(const ModuleContext& context, int index, const HtmlOptions& options) {
    const SemanticNode& node = context.Node(index);
    // The fragment of the parent is an ordgroup, so it renders as a "mord"
    // span.  Its children are the label, the arrow and the other label.
    NodeVector children;
    for (int child : context.Children(node.FirstChild)) {
        children.push_back(BuildCdNode(context, child, options));
    }
    HtmlNodePtr parent = MakeSpan({"mord"}, std::move(children), options);
    parent->AddClass("cd-vert-arrow");
    return parent;
}

}  // namespace

HtmlNodePtr BuildArrayNode(const ModuleContext& context, int index, const HtmlOptions& options) {
    const SemanticNode& matrix = context.Node(index);
    const std::u16string env_name(text(matrix.Text));

    ArrayParams params;
    params.Name = TrimStar(env_name);
    params.Family = FamilyOf(params.Name);

    // The environment's layout parameters, following KaTeX's environment
    // handlers.
    switch (params.Family) {
        case EnvFamily::Array:
            params.HskipBeforeAndAfter = true;  // \@preamble in lttab.dtx
            break;
        case EnvFamily::SubArray:
            params.ArrayStretch = 0.5;
            break;
        case EnvFamily::SmallMatrix:
            params.ArrayStretch = 0.5;
            params.Small = true;  // KaTeX's `colSeparationType === "small"`
            break;
        case EnvFamily::Cases:
            params.ArrayStretch = 1.2;
            break;
        case EnvFamily::Aligned:
            params.AddJot = true;
            params.EmptySingleRow = true;
            params.AlignedAt = params.Name == u"alignedat"_u16 || params.Name == u"alignat"_u16 ||
                               params.Name == u"alignat*"_u16;
            break;
        case EnvFamily::Gathered:
            params.AddJot = true;
            params.EmptySingleRow = true;
            break;
        case EnvFamily::Equation:
            params.EmptySingleRow = true;
            break;
        case EnvFamily::CD:
            params.CD = true;
            params.AddJot = true;
            break;
        case EnvFamily::Matrix:
            break;
    }
    // An explicit `\arraystretch` overrides the environment's default.
    if (matrix.Value2 != 0.0) {
        params.ArrayStretch = matrix.Value2;
    }

    const Style cell_style = CellStyleOf(env_name);

    // ---------------------------------------------------------------- rows
    IndexVector row_indices = context.Children(index);

    // KaTeX drops a trailing empty row, unless the environment keeps empty
    // single rows and the row is the only one.
    const auto row_is_empty = [&](int row_index) {
        const IndexVector cells = context.Children(row_index);
        if (cells.size() != 1) return false;
        const int style_node = context.Node(cells[0]).FirstChild;
        if (style_node < 0) return false;
        const int body = context.Node(style_node).FirstChild;
        if (body < 0) return false;
        return context.Children(body).empty();
    };
    int trailing_rules = matrix.RuleBelow;
    if (!row_indices.empty() && row_is_empty(row_indices.back()) &&
        (row_indices.size() > 1 || !params.EmptySingleRow) &&
        context.Node(row_indices.back()).SecondChild < 0) {
        // The rules in front of the dropped row become the rules behind the
        // last remaining row.
        trailing_rules = context.Node(row_indices.back()).RuleAbove;
        row_indices.pop_back();
    }

    const int nr = static_cast<int>(row_indices.size());

    // ------------------------------------------------------------- columns
    int nc = 0;
    for (int row_index : row_indices) {
        nc = std::max(nc, static_cast<int>(context.Children(row_index).size()));
    }

    std::vector<AlignSpec> cols;
    switch (params.Family) {
        case EnvFamily::Array:
        case EnvFamily::SubArray:
            cols = ParseColumnSpec(text(matrix.Alignment));
            break;
        case EnvFamily::Cases:
            cols = CasesColumnSpec();
            break;
        case EnvFamily::Aligned: {
            // The `alignedat` family states its number of column pairs
            // explicitly; the other environments use the widest row.
            const int num_cols = params.AlignedAt ? 2 * static_cast<int>(matrix.Value) : nc;
            cols = AlignedColumnSpec(num_cols, !params.AlignedAt);
            break;
        }
        case EnvFamily::Gathered:
            cols.assign(1, AlignSpec{false, 0, "c", false, 0.0, false, 0.0});
            break;
        case EnvFamily::Matrix: {
            // All columns of a matrix share the same alignment.
            const std::string align = text(matrix.Alignment) == u"l"_u16   ? "l"
                                      : text(matrix.Alignment) == u"r"_u16 ? "r"
                                                                       : "c";
            cols.assign(static_cast<size_t>(nc), AlignSpec{false, 0, align, false, 0.0, false, 0.0});
            break;
        }
        case EnvFamily::SmallMatrix:
        case EnvFamily::Equation:
            break;
        case EnvFamily::CD: {
            // The CD package sets \enskip between the columns, so half an
            // \enskip goes in front of and behind every column.  KaTeX takes
            // the number of the column descriptions from the first row of the
            // environment, so the other columns use the default column
            // separation (`src/environments/cd.ts`).
            const size_t described = row_indices.empty()
                                         ? 0
                                         : context.Children(row_indices.front()).size();
            cols.assign(described, AlignSpec{false, 0, "c", true, 0.25, true, 0.25});
            break;
        }
    }

    // ----------------------------------------------------------- metrics
    const GlobalMetrics& metrics = options.Metrics();
    const double rule_thickness = std::max(metrics.ArrayRuleWidth, options.MinRuleThickness);

    // Horizontal spacing.
    const double pt = 1.0 / metrics.PtPerEm;
    double arraycolsep = 5 * pt;  // default value, i.e. \arraycolsep in article.cls
    if (params.Small) {
        // We're in a {smallmatrix}. Default column space is \thickspace,
        // i.e. 5/18em = 0.2778em, per amsmath.dtx for {smallmatrix}.
        const double local_multiplier = options.HavingStyle(Style(4, 2, false)).SizeMultiplier;
        arraycolsep = 0.2778 * (local_multiplier / options.SizeMultiplier);
    }

    // Vertical spacing.
    const double baselineskip = params.CD ? CalculateSize(3, TexUnit::Ex, options) : 12 * pt;
    const double jot = 3 * pt;  // Default \jot from ltmath.dtx
    const double arrayskip = params.ArrayStretch * baselineskip;
    const double arstrut_height = 0.7 * arrayskip;  // \@arstrutbox in lttab.dtx
    const double arstrut_depth = 0.3 * arrayskip;

    // --------------------------------------------------------- row metrics
    std::vector<OutRow> body(static_cast<size_t>(nr));
    double total_height = 0.0;
    std::vector<HLine> hlines;

    // Sets the position of the `\hline`s in one gap.  `count` is positive for
    // solid rules and negative for dashed ones.
    const auto set_hline_pos = [&](int count) {
        const bool dashed = count < 0;
        const int total = count < 0 ? -count : count;
        for (int i = 0; i < total; ++i) {
            if (i > 0) {
                total_height += 0.25;
            }
            hlines.push_back(HLine{total_height, dashed});
        }
    };

    // The rules in front of the first row.
    if (!row_indices.empty()) {
        set_hline_pos(context.Node(row_indices.front()).RuleAbove);
    }

    for (int r = 0; r < nr; ++r) {
        const SemanticNode& row = context.Node(row_indices[static_cast<size_t>(r)]);
        OutRow& out_row = body[static_cast<size_t>(r)];
        double height = arstrut_height;  // \@array adds an \@arstrut
        double depth = arstrut_depth;    // to each row (via the template)

        const IndexVector cell_indices = context.Children(row_indices[static_cast<size_t>(r)]);
        out_row.Cells.resize(cell_indices.size());
        for (size_t c = 0; c < cell_indices.size(); ++c) {
            const SemanticNode& cell = context.Node(cell_indices[c]);
            // KaTeX wraps every cell in a "styling" node; the C++ tree keeps
            // the style in a Style node whose child holds the cell's row.
            const int style_node = cell.FirstChild;
            const int body_node = style_node < 0 ? -1 : context.Node(style_node).FirstChild;
            if (body_node < 0) {
                out_row.Cells[c] = MakeSpan({}, {});
                continue;
            }
            const HtmlOptions cell_options =
                options.HavingStyle(style_node < 0 ? cell_style : StyleOf(context.Node(style_node).Style));
            // KaTeX's cells hold an ordgroup, which is rendered as a "mord"
            // span.  The CD environment stores the cell's content directly,
            // without that wrapper; the parser marks such a row with `Value`.
            const bool transparent = context.Node(body_node).Value == 1;
            NodeVector elements;
            if (transparent) {
                for (int child : context.Children(body_node)) {
                    elements.push_back(BuildCdNode(context, child, cell_options));
                }
            } else {
                elements.push_back(context.build_group(body_node, cell_options));
            }
            if (elements.empty() && !transparent) {
                // An empty cell of a {CD} environment renders as an empty
                // fragment, exactly like the "styling" group that KaTeX uses
                // for it (`src/environments/cd.ts`).
                elements.push_back(MakeSpan({}, {}));
            }
            // KaTeX's `sizingGroup` adds the size-resetting classes to the
            // cell and scales its height and depth.
            const bool has_sizing = cell_options.Size != options.Size;
            const double multiplier = cell_options.SizeMultiplier / options.SizeMultiplier;
            for (auto& element : elements) {
                if (element == nullptr) {
                    element = MakeSpan({}, {});
                }
                if (has_sizing) {
                    const size_t position = element->Classes.IndexOf("katex-sizing");
                    const std::string_view reset_size = HtmlOptions::ResetSizeClassName(cell_options.Size);
                    if (position == static_cast<size_t>(-1)) {
                        cell_options.AddSizingClasses(element->Classes, options);
                    } else if (position + 1 < element->Classes.Size() && element->Classes.At(position + 1) == reset_size) {
                        element->Classes.SetAt(position + 1, HtmlOptions::ResetSizeClassName(options.Size));
                    }
                }
                element->Height *= multiplier;
                element->Depth *= multiplier;
                if (depth < element->Depth) {
                    depth = element->Depth;
                }
                if (height < element->Height) {
                    height = element->Height;
                }
            }
            out_row.Cells[c] = transparent ? MakeFragment(std::move(elements)) : std::move(elements.front());
        }

        // The optional size of the row separator (`\\[1em]`).
        double gap = 0.0;
        if (row.Value != 0.0) {
            gap = CalculateSize(row.Length1.Number, row.Length1.Unit, options);
            if (gap > 0.0) {  // \@argarraycr
                gap += arstrut_depth;
                if (depth < gap) {
                    depth = gap;  // \@xargarraycr
                }
                gap = 0.0;
            }
        }
        // In AMS multiline environments such as aligned and gathered, rows
        // correspond to lines that have additional \jot added between lines
        // via \openup.
        if (params.AddJot && r < nr - 1) {
            depth += jot;
        }

        out_row.Height = height;
        out_row.Depth = depth;
        total_height += height;
        out_row.Pos = total_height;
        total_height += depth + gap;  // \@yargarraycr

        // The rules in front of the next row.
        if (r + 1 < nr) {
            set_hline_pos(context.Node(row_indices[static_cast<size_t>(r + 1)]).RuleAbove);
        }
    }
    // The rules behind the last row.
    set_hline_pos(trailing_rules);

    const double offset = total_height / 2 + metrics.AxisHeight;

    // ------------------------------------------------------------- columns
    NodeVector column_nodes;
    for (int c = 0, col_descr_num = 0;
         // Continue while either there are more columns or more column
         // descriptions, so trailing separators don't get lost.
         c < nc || col_descr_num < static_cast<int>(cols.size());
         ++c, ++col_descr_num) {
        const AlignSpec* col_descr =
            col_descr_num < static_cast<int>(cols.size()) ? &cols[static_cast<size_t>(col_descr_num)] : nullptr;

        bool first_separator = true;
        while (col_descr != nullptr && col_descr->IsSeparator) {
            // If there is more than one separator in a row, add a space
            // between them.
            if (!first_separator) {
                auto col_sep = MakeSpan({"arraycolsep"}, NodeVector{});
                col_sep->Style.SetEm("width", metrics.DoubleRuleSep);
                column_nodes.push_back(std::move(col_sep));
            }

            const std::string line_type = col_descr->Separator == u'|' ? "solid" : "dashed";
            auto separator = MakeSpan({"vertical-separator"}, NodeVector{}, options);
            separator->Style.SetEm("height", total_height);
            separator->Style.SetEm("border-right-width", rule_thickness);
            separator->Style.Set("border-right-style", line_type);
            separator->Style.Set("margin", "0 " + MakeEm(-rule_thickness / 2));
            const double shift = total_height - offset;
            if (shift != 0.0) {
                separator->Style.SetEm("vertical-align", -shift);
            }
            column_nodes.push_back(std::move(separator));

            ++col_descr_num;
            col_descr = col_descr_num < static_cast<int>(cols.size()) ? &cols[static_cast<size_t>(col_descr_num)]
                                                                      : nullptr;
            first_separator = false;
        }

        if (c >= nc) {
            continue;
        }

        double sepwidth = 0.0;
        if (c > 0 || params.HskipBeforeAndAfter) {
            sepwidth = (col_descr != nullptr && col_descr->HasPregap) ? col_descr->Pregap : arraycolsep;
            if (sepwidth != 0.0) {
                auto col_sep = MakeSpan({"arraycolsep"}, NodeVector{});
                col_sep->Style.SetEm("width", sepwidth);
                column_nodes.push_back(std::move(col_sep));
            }
        }

        VListElements col_elems;
        for (int r = 0; r < nr; ++r) {
            OutRow& row = body[static_cast<size_t>(r)];
            if (c >= static_cast<int>(row.Cells.size())) {
                continue;
            }
            HtmlNodePtr elem = std::move(row.Cells[static_cast<size_t>(c)]);
            const double shift = row.Pos - offset;
            elem->Depth = row.Depth;
            elem->Height = row.Height;
            VListElement element;
            element.Elem = std::move(elem);
            element.Shift = shift;
            col_elems.push_back(std::move(element));
        }

        VListParams vlist_params;
        vlist_params.PositionType = VListPositionType::IndividualShift;
        vlist_params.ShiftedElements = std::move(col_elems);
        NodePtr<HtmlSpan> col_vlist = MakeVList(std::move(vlist_params), options);
        const std::string align = (col_descr != nullptr && !col_descr->Align.empty()) ? col_descr->Align : "c";
        NodeVector col_children;
        col_children.push_back(std::move(col_vlist));
        // KaTeX writes the alignment of the column as the class "col-align-"
        // followed by the alignment.  The alignments are a fixed set, so the
        // names are entries of a table: a class list holds them as views, and
        // a view of a temporary string would dangle.
        static constexpr const char* kColumnAlignClasses[] = {"col-align-l", "col-align-c",
                                                               "col-align-r"};
        const char* align_class = kColumnAlignClasses[1];
        if (same_text(align, "l")) {
            align_class = kColumnAlignClasses[0];
        } else if (same_text(align, "r")) {
            align_class = kColumnAlignClasses[2];
        } else if (align != "c") {
            // An alignment outside the set of KaTeX is copied into the arena,
            // so that the name lives as long as the render.
            align_class = CopyToNodeArena("col-align-" + align).data();
        }
        auto col_span = MakeSpan({align_class}, std::move(col_children));
        column_nodes.push_back(std::move(col_span));

        if (c < nc - 1 || params.HskipBeforeAndAfter) {
            sepwidth = (col_descr != nullptr && col_descr->HasPostgap) ? col_descr->Postgap : arraycolsep;
            if (sepwidth != 0.0) {
                auto col_sep = MakeSpan({"arraycolsep"}, NodeVector{});
                col_sep->Style.SetEm("width", sepwidth);
                column_nodes.push_back(std::move(col_sep));
            }
        }
    }

    HtmlNodePtr table_body = MakeSpan({"mtable"}, std::move(column_nodes));

    // Add \hline(s), if any.
    if (!hlines.empty()) {
        VListElements vlist_elems;
        VListElement table_element;
        table_element.Elem = std::move(table_body);
        table_element.Shift = 0.0;
        vlist_elems.push_back(std::move(table_element));
        while (!hlines.empty()) {
            const HLine hline = hlines.back();
            hlines.pop_back();
            VListElement element;
            element.Elem = MakeLineSpan(hline.IsDashed ? "katex-hdashline" : "katex-hline", options,
                                        rule_thickness);
            element.Shift = hline.Pos - offset;
            vlist_elems.push_back(std::move(element));
        }
        VListParams vlist_params;
        vlist_params.PositionType = VListPositionType::IndividualShift;
        vlist_params.ShiftedElements = std::move(vlist_elems);
        table_body = MakeVList(std::move(vlist_params), options);
    }

    // ---------------------------------------------------------- tag column
    // KaTeX adds a tag column when at least one row carries a tag.  Rows
    // that were suppressed with `\nonumber` still reserve their place in the
    // column, but with an empty span.
    VListElements tag_spans;
    bool has_tag = false;
    for (int r = 0; r < nr; ++r) {
        const SemanticNode& row = context.Node(row_indices[static_cast<size_t>(r)]);
        const int tag = row.SecondChild;
        const OutRow& out_row = body[static_cast<size_t>(r)];
        if (tag < 0) {
            if (!row.NeverLimits) {
                continue;
            }
            VListElement element;
            element.Elem = MakeSpan({}, NodeVector{}, options);
            element.Elem->Depth = out_row.Depth;
            element.Elem->Height = out_row.Height;
            element.Shift = out_row.Pos - offset;
            tag_spans.push_back(std::move(element));
            continue;
        }
        has_tag = true;
        HtmlNodePtr tag_span;
        if (row.AlwaysLimits) {
            // An automatically numbered row; the number itself is produced by
            // a CSS counter.
            tag_span = MakeSpan({"eqn-num"}, NodeVector{}, options);
        } else {
            // A manual `\tag{...}`.  KaTeX builds the tag's expression into a
            // span without a math class.
            NodeVector tag_children;
            tag_children.push_back(context.build_group(tag, options));
            tag_span = MakeSpan({}, std::move(tag_children), options);
        }
        tag_span->Depth = out_row.Depth;
        tag_span->Height = out_row.Height;
        VListElement element;
        element.Elem = std::move(tag_span);
        element.Shift = out_row.Pos - offset;
        tag_spans.push_back(std::move(element));
    }

    HtmlNodePtr result;
    if (!has_tag) {
        NodeVector mord_children;
        mord_children.push_back(std::move(table_body));
        result = MakeSpan({"mord"}, std::move(mord_children), options);
    } else {
        VListParams vlist_params;
        vlist_params.PositionType = VListPositionType::IndividualShift;
        vlist_params.ShiftedElements = std::move(tag_spans);
        NodePtr<HtmlSpan> eqn_num_col = MakeVList(std::move(vlist_params), options);
        NodeVector tag_children;
        tag_children.push_back(std::move(eqn_num_col));
        auto tag_col = MakeSpan({"katex-tag"}, std::move(tag_children), options);
        NodeVector fragment_children;
        fragment_children.push_back(std::move(table_body));
        fragment_children.push_back(std::move(tag_col));
        result = MakeFragment(std::move(fragment_children));
    }

    // ------------------------------------------------------------ delimiters
    // KaTeX wraps the matrix and cases environments in a `leftright` node so
    // that the delimiters are sized to the array.
    const bool has_delimiters = params.Family == EnvFamily::Matrix   ? params.Name != u"matrix"_u16
                                : params.Family == EnvFamily::Cases ? true
                                                                   : false;
    if (!has_delimiters) {
        return result;
    }

    double inner_height = result->Height;
    double inner_depth = result->Depth;
    // The size of delimiters is the same, regardless of what style we are in.
    inner_height *= options.SizeMultiplier;
    inner_depth *= options.SizeMultiplier;

    NodeVector inner;
    if (matrix.LeftDelimiter == 0) {
        inner.push_back(MakeArrayNullDelimiter(options, {"mopen"}));
    } else {
        inner.push_back(MakeLeftRightDelim(DelimiterName(matrix.LeftDelimiter), inner_height, inner_depth,
                                           options, Mode::Math, {"mopen"}));
    }
    inner.push_back(std::move(result));
    if (matrix.RightDelimiter == 0) {
        inner.push_back(MakeArrayNullDelimiter(options, {"mclose"}));
    } else {
        inner.push_back(MakeLeftRightDelim(DelimiterName(matrix.RightDelimiter), inner_height, inner_depth,
                                           options, Mode::Math, {"mclose"}));
    }
    return MakeSpan({"minner"}, std::move(inner), options);
}

}  // namespace velocitex::html
