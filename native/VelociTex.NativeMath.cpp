// VelociTex fast LaTeX -> presentation MathML parser.
// This is a bounded, allocation-light fast path for common math. It returns
// unsupported for constructs it cannot represent faithfully; the caller must
// fall back to the full C# engine. No global mutable state: one thread_local
// output buffer, so concurrent calls on different threads are safe.
#include <cstdint>
#include <cstring>
#include <string>

#if defined(_WIN32)
#define VT_EXPORT extern "C" __declspec(dllexport)
#else
#define VT_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
constexpr int kMaxInput = 16384;
constexpr int kMaxDepth = 64;
constexpr int kMaxNodes = 4096;

struct Parser {
    const char* p;
    const char* end;
    std::string& out;
    bool display;
    int depth = 0;
    int nodes = 0;
    bool ok = true;

    Parser(const char* begin, const char* finish, std::string& output, bool block)
        : p(begin), end(finish), out(output), display(block) {}

    bool Starts(const char* text) const {
        size_t n = std::strlen(text);
        return static_cast<size_t>(end - p) >= n && std::memcmp(p, text, n) == 0;
    }
    bool Take(char c) {
        if (p < end && *p == c) {
            ++p;
            return true;
        }
        return false;
    }
    bool Take(const char* text) {
        size_t n = std::strlen(text);
        if (static_cast<size_t>(end - p) < n || std::memcmp(p, text, n) != 0)
            return false;
        p += n;
        return true;
    }
    void Space() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
            ++p;
    }
    void Raw(const char* s, size_t n) { out.append(s, n); }
    void Raw(const char* s) { out.append(s); }
    void Escaped(const char* s, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            switch (s[i]) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            default:
                out += s[i];
            }
        }
    }
    void EscapedText(const char* s, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            switch (s[i]) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case ' ':
                out += "&#160;";
                break;
            case '\t':
                out += "&#160;&#160;&#160;&#160;";
                break;
            default:
                out += s[i];
            }
        }
    }
    bool Node() {
        if (++nodes > kMaxNodes) {
            ok = false;
            return false;
        }
        return true;
    }
    void Open(const char* tag) {
        out += '<';
        out += tag;
        out += '>';
    }
    void Close(const char* tag) {
        out += "</";
        out += tag;
        out += '>';
    }
    void Empty(const char* tag) {
        out += '<';
        out += tag;
        out += "/>";
    }

    bool Group(std::string& target) {
        if (!Take('{'))
            return false;
        ++depth;
        if (depth > kMaxDepth) {
            ok = false;
            return false;
        }
        std::string inner;
        inner.reserve(64);
        Parser nested(p, end, inner, display);
        nested.depth = depth;
        if (!nested.Row('\0')) {
            ok = false;
            return false;
        }
        if (!nested.Take('}')) {
            ok = false;
            return false;
        }
        p = nested.p;
        target = std::move(inner);
        --depth;
        return true;
    }

    bool Arg(std::string& target) {
        Space();
        if (p >= end) {
            ok = false;
            return false;
        }
        if (*p == '{')
            return Group(target);
        std::string one;
        one.reserve(32);
        Parser nested(p, end, one, display);
        nested.depth = depth + 1;
        if (!nested.Atom()) {
            ok = false;
            return false;
        }
        p = nested.p;
        target = std::move(one);
        return true;
    }

    void Delimiter(const char* token, size_t len) {
        // token is the raw delimiter text after \left / \right / \big...
        if (len == 1 && token[0] == '.')
            return;
        out += "<mo>";
        if (len == 7 && std::memcmp(token, "\\langle", 7) == 0)
            out += "⟨";
        else if (len == 7 && std::memcmp(token, "\\rangle", 7) == 0)
            out += "⟩";
        else if (len == 7 && std::memcmp(token, "\\lfloor", 7) == 0)
            out += "⌊";
        else if (len == 7 && std::memcmp(token, "\\rfloor", 7) == 0)
            out += "⌋";
        else if (len == 6 && std::memcmp(token, "\\lceil", 6) == 0)
            out += "⌈";
        else if (len == 6 && std::memcmp(token, "\\rceil", 6) == 0)
            out += "⌉";
        else if (len == 5 && std::memcmp(token, "\\vert", 5) == 0)
            out += "|";
        else if (len == 5 && std::memcmp(token, "\\Vert", 5) == 0)
            out += "‖";
        else if (len == 2 && token[0] == '\\') {
            switch (token[1]) {
            case '{':
                out += '{';
                break;
            case '}':
                out += '}';
                break;
            case '|':
                out += "‖";
                break;
            case 'l':
                out += "⟨";
                break;
            case 'r':
                out += "⟩";
                break;
            default:
                out += token[1];
                break;
            }
        } else if (len == 1)
            out += token[0];
        else
            Escaped(token, len);
        out += "</mo>";
    }

    bool DelimiterToken(const char*& token, size_t& len) {
        Space();
        if (p >= end) {
            ok = false;
            return false;
        }
        token = p;
        if (*p == '\\') {
            ++p;
            if (p >= end) {
                ok = false;
                return false;
            }
            // Named delimiters.
            if (Starts("langle")) {
                p += 6;
                len = 7;
                return true;
            }
            if (Starts("rangle")) {
                p += 6;
                len = 7;
                return true;
            }
            if (Starts("lfloor")) {
                p += 6;
                len = 7;
                return true;
            }
            if (Starts("rfloor")) {
                p += 6;
                len = 7;
                return true;
            }
            if (Starts("lceil")) {
                p += 5;
                len = 6;
                return true;
            }
            if (Starts("rceil")) {
                p += 5;
                len = 6;
                return true;
            }
            if (Starts("vert")) {
                p += 4;
                len = 5;
                return true;
            }
            if (Starts("Vert")) {
                p += 4;
                len = 5;
                return true;
            }
            ++p;
            len = 2;
            return true;
        }
        ++p;
        len = 1;
        return true;
    }

    bool GroupText(std::string& target) {
        if (!Take('{'))
            return false;
        int braces = 1;
        const char* start = p;
        while (p < end && braces != 0) {
            if (*p == '\\' && p + 1 < end) {
                p += 2;
                continue;
            }
            if (*p == '{')
                ++braces;
            else if (*p == '}')
                --braces;
            if (braces != 0)
                ++p;
        }
        if (braces != 0) {
            ok = false;
            return false;
        }
        target.assign(start, static_cast<size_t>(p - start));
        ++p;
        return true;
    }

    bool Row(char stop) {
        if (++depth > kMaxDepth) {
            ok = false;
            return false;
        }
        Space();
        size_t mark = out.size();
        int count = 0;
        while (p < end && ok) {
            Space();
            if (p >= end)
                break;
            if (stop && *p == stop)
                break;
            if (*p == '}')
                break;
            if (*p == '&')
                break;
            if (Starts("\\\\"))
                break;
            if (Starts("\\right"))
                break;
            if (Starts("\\end"))
                break;
            if (Starts("\\middle"))
                break;
            if (!Atom()) {
                ok = false;
                break;
            }
            ++count;
        }
        if (count > 1)
            out.insert(mark, "<mrow>");
        if (count > 1)
            out += "</mrow>";
        --depth;
        return ok;
    }

    bool Scripts(size_t baseStart, bool large) {
        bool sub = false, sup = false;
        Space();
        while (p < end && (*p == '_' || *p == '^' || *p == '\'')) {
            if (*p == '\'') {
                ++p;
                out += "<mo>′</mo>";
                continue;
            }
            bool isSub = *p == '_';
            ++p;
            Space();
            std::string arg;
            if (*p == '{') {
                if (!Group(arg))
                    return false;
            } else {
                Parser nested(p, end, arg, display);
                nested.depth = depth + 1;
                if (!nested.Atom()) {
                    ok = false;
                    return false;
                }
                p = nested.p;
            }
            if (isSub)
                sub = true;
            else
                sup = true;
            // Append after base; wrapper is inserted below.
            out += arg;
            Space();
        }
        if (!sub && !sup)
            return true;
        const char* open = large && display ? (sub && sup ? "munderover"
                                               : sub      ? "munder"
                                                          : "mover")
                                            : (sub && sup ? "msubsup"
                                               : sub      ? "msub"
                                                          : "msup");
        std::string opening = "<";
        opening += open;
        opening += '>';
        out.insert(baseStart, opening);
        out += "</";
        out += open;
        out += '>';
        return true;
    }

    bool Atom() {
        Space();
        if (p >= end || !Node()) {
            ok = false;
            return false;
        }
        size_t start = out.size();
        char c = *p;
        if (c == '{') {
            std::string group;
            if (!Group(group))
                return false;
            out += group;
            return Scripts(start, false);
        }
        if (c == '\\') {
            ++p;
            if (p >= end) {
                ok = false;
                return false;
            }
            return Command();
        }
        if (c == '&' || c == '}' || c == '^' || c == '_') {
            ok = false;
            return false;
        }
        if (c >= '0' && c <= '9') {
            const char* s = p;
            while (p < end && ((*p >= '0' && *p <= '9') || *p == '.'))
                ++p;
            out += "<mn>";
            Escaped(s, static_cast<size_t>(p - s));
            out += "</mn>";
            return Scripts(start, false);
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            char text[2] = {c, 0};
            ++p;
            out += "<mi>";
            out += text;
            out += "</mi>";
            return Scripts(start, false);
        }
        if (c == '+') {
            ++p;
            out += "<mo>+</mo>";
            return Scripts(start, false);
        }
        if (c == '-') {
            ++p;
            out += "<mo>−</mo>";
            return Scripts(start, false);
        }
        if (c == '=') {
            ++p;
            out += "<mo>=</mo>";
            return Scripts(start, false);
        }
        if (c == '<') {
            ++p;
            out += "<mo>&lt;</mo>";
            return Scripts(start, false);
        }
        if (c == '>') {
            ++p;
            out += "<mo>&gt;</mo>";
            return Scripts(start, false);
        }
        if (c == '*' || c == '/' || c == '!' || c == ':' || c == ';' || c == ',' || c == '|') {
            char text[2] = {c, 0};
            ++p;
            out += "<mo>";
            out += text;
            out += "</mo>";
            return Scripts(start, false);
        }
        if (c == '(' || c == ')' || c == '[' || c == ']') {
            out += "<mo>";
            out += c;
            out += "</mo>";
            ++p;
            return Scripts(start, false);
        }
        if (c == '~' || c == ' ') {
            ++p;
            out += "<mspace width=\"0.333em\"/>";
            return Scripts(start, false);
        }
        // Non-ASCII outside \text is not mapped by this fast path.
        ok = false;
        return false;
    }

    bool Command() {
        size_t start = out.size();
        const char* nameStart = p;
        while (p < end && (*p >= 'a' && *p <= 'z' || *p >= 'A' && *p <= 'Z'))
            ++p;
        if (p == nameStart) {
            // Escaped punctuation: \, \; \! \   \{ \} \% \# \_ etc.
            char c = *p++;
            switch (c) {
            case ',':
                out += "<mspace width=\"0.1667em\"/>";
                return true;
            case ':':
                out += "<mspace width=\"0.2222em\"/>";
                return true;
            case ';':
                out += "<mspace width=\"0.2778em\"/>";
                return true;
            case '!':
                out += "<mspace width=\"-0.1667em\"/>";
                return true;
            case ' ':
                out += "<mspace width=\"0.333em\"/>";
                return true;
            case '\\':
                ok = false;
                return false;
            case '{':
                out += "<mo>{</mo>";
                return Scripts(start, false);
            case '}':
                out += "<mo>}</mo>";
                return Scripts(start, false);
            case '%':
                out += "<mo>%</mo>";
                return Scripts(start, false);
            case '#':
                out += "<mo>#</mo>";
                return Scripts(start, false);
            case '_':
                out += "<mo>_</mo>";
                return Scripts(start, false);
            default:
                ok = false;
                return false;
            }
        }
        auto is = [&](const char* s) {
            return static_cast<size_t>(p - nameStart) == std::strlen(s) &&
                   std::memcmp(nameStart, s, static_cast<size_t>(p - nameStart)) == 0;
        };

        if (is("frac") || is("dfrac") || is("tfrac")) {
            std::string a, b;
            if (!Arg(a) || !Arg(b))
                return false;
            out += "<mfrac>";
            out += a;
            out += b;
            out += "</mfrac>";
            return Scripts(start, false);
        }
        if (is("sqrt")) {
            Space();
            bool root = false;
            std::string index;
            if (p < end && *p == '[') {
                ++p;
                if (!Row(']'))
                    return false;
                if (!Take(']')) {
                    ok = false;
                    return false;
                }
                index.assign(out, start, out.size() - start);
                out.resize(start);
                root = true;
            }
            std::string body;
            if (!Arg(body))
                return false;
            if (root) {
                out += "<mroot>";
                out += body;
                out += index;
                out += "</mroot>";
            } else {
                out += "<msqrt>";
                out += body;
                out += "</msqrt>";
            }
            return Scripts(start, false);
        }
        if (is("text") || is("textrm") || is("mbox")) {
            std::string text;
            if (!GroupText(text))
                return false;
            out += "<mtext>";
            EscapedText(text.data(), text.size());
            out += "</mtext>";
            return Scripts(start, false);
        }
        if (is("operatorname")) {
            std::string text;
            if (!GroupText(text))
                return false;
            out += "<mi>";
            EscapedText(text.data(), text.size());
            out += "</mi>";
            return Scripts(start, false);
        }
        if (is("mathrm") || is("mathbf") || is("mathit") || is("mathsf") || is("mathtt")) {
            const char* variant = is("mathrm")   ? "normal"
                                  : is("mathbf") ? "bold"
                                  : is("mathit") ? "italic"
                                  : is("mathsf") ? "sans-serif"
                                                 : "monospace";
            std::string body;
            if (!Arg(body))
                return false;
            out += "<mrow mathvariant=\"";
            out += variant;
            out += "\">";
            out += body;
            out += "</mrow>";
            return Scripts(start, false);
        }
        if (is("left")) {
            const char* token;
            size_t len;
            if (!DelimiterToken(token, len))
                return false;
            out += "<mrow>";
            Delimiter(token, len);
            if (!Row('\0'))
                return false;
            Space();
            if (!Take("\\right")) {
                ok = false;
                return false;
            }
            if (!DelimiterToken(token, len))
                return false;
            Delimiter(token, len);
            out += "</mrow>";
            return Scripts(start, false);
        }
        if (is("right") || is("end") || is("middle")) {
            ok = false;
            return false;
        }
        if (is("begin")) {
            std::string env;
            if (!GroupText(env))
                return false;
            return Environment(env, start);
        }
        if (is("overline") || is("bar") || is("hat") || is("vec") || is("tilde")) {
            std::string body;
            if (!Arg(body))
                return false;
            const char* accent = is("overline") || is("bar") ? "¯" : is("hat") ? "^" : is("vec") ? "→" : "~";
            out += "<mover accent=\"true\">";
            out += body;
            out += "<mo>";
            out += accent;
            out += "</mo></mover>";
            return Scripts(start, false);
        }
        if (is("underline")) {
            std::string body;
            if (!Arg(body))
                return false;
            out += "<munder accentunder=\"true\">";
            out += body;
            out += "<mo>_</mo></munder>";
            return Scripts(start, false);
        }
        if (is("boxed")) {
            std::string body;
            if (!Arg(body))
                return false;
            out += "<menclose notation=\"box\">";
            out += body;
            out += "</menclose>";
            return Scripts(start, false);
        }
        if (is("phantom") || is("hphantom") || is("vphantom")) {
            std::string body;
            if (!Arg(body))
                return false;
            out += "<mphantom>";
            out += body;
            out += "</mphantom>";
            return Scripts(start, false);
        }
        if (is("quad")) {
            out += "<mspace width=\"1em\"/>";
            return true;
        }
        if (is("qquad")) {
            out += "<mspace width=\"2em\"/>";
            return true;
        }
        if (is("limits") || is("nolimits") || is("displaystyle") || is("textstyle"))
            return true;
        if (is("big") || is("Big") || is("bigg") || is("Bigg") || is("bigl") || is("Bigl") || is("biggl") ||
            is("Biggl") || is("bigr") || is("Bigr") || is("biggr") || is("Biggr")) {
            Space();
            if (p < end) {
                const char* s = p;
                size_t n = 1;
                ++p;
                Delimiter(s, n);
            }
            return Scripts(start, false);
        }

        struct Symbol {
            const char* name;
            const char* text;
            bool operatorLike;
            bool large;
        };
        static const Symbol symbols[] = {
            {"alpha", "α", false, false},
            {"beta", "β", false, false},
            {"gamma", "γ", false, false},
            {"delta", "δ", false, false},
            {"epsilon", "ϵ", false, false},
            {"varepsilon", "ε", false, false},
            {"zeta", "ζ", false, false},
            {"eta", "η", false, false},
            {"theta", "θ", false, false},
            {"vartheta", "ϑ", false, false},
            {"iota", "ι", false, false},
            {"kappa", "κ", false, false},
            {"lambda", "λ", false, false},
            {"mu", "μ", false, false},
            {"nu", "ν", false, false},
            {"xi", "ξ", false, false},
            {"pi", "π", false, false},
            {"rho", "ρ", false, false},
            {"sigma", "σ", false, false},
            {"tau", "τ", false, false},
            {"upsilon", "υ", false, false},
            {"phi", "ϕ", false, false},
            {"varphi", "φ", false, false},
            {"chi", "χ", false, false},
            {"psi", "ψ", false, false},
            {"omega", "ω", false, false},
            {"Gamma", "Γ", false, false},
            {"Delta", "Δ", false, false},
            {"Theta", "Θ", false, false},
            {"Lambda", "Λ", false, false},
            {"Xi", "Ξ", false, false},
            {"Pi", "Π", false, false},
            {"Sigma", "Σ", false, false},
            {"Phi", "Φ", false, false},
            {"Psi", "Ψ", false, false},
            {"Omega", "Ω", false, false},
            {"infty", "∞", true, false},
            {"partial", "∂", false, false},
            {"nabla", "∇", true, false},
            {"to", "→", true, false},
            {"rightarrow", "→", true, false},
            {"leftarrow", "←", true, false},
            {"Rightarrow", "⇒", true, false},
            {"Leftarrow", "⇐", true, false},
            {"leftrightarrow", "↔", true, false},
            {"le", "≤", true, false},
            {"leq", "≤", true, false},
            {"ge", "≥", true, false},
            {"geq", "≥", true, false},
            {"ne", "≠", true, false},
            {"neq", "≠", true, false},
            {"approx", "≈", true, false},
            {"sim", "∼", true, false},
            {"pm", "±", true, false},
            {"mp", "∓", true, false},
            {"times", "×", true, false},
            {"cdot", "⋅", true, false},
            {"div", "÷", true, false},
            {"ldots", "…", true, false},
            {"cdots", "⋯", true, false},
            {"vdots", "⋮", true, false},
            {"dots", "…", true, false},
            {"prime", "′", true, false},
            {"degree", "°", true, false},
            {"sin", "sin", false, false},
            {"cos", "cos", false, false},
            {"tan", "tan", false, false},
            {"cot", "cot", false, false},
            {"sec", "sec", false, false},
            {"csc", "csc", false, false},
            {"arcsin", "arcsin", false, false},
            {"arccos", "arccos", false, false},
            {"arctan", "arctan", false, false},
            {"sinh", "sinh", false, false},
            {"cosh", "cosh", false, false},
            {"tanh", "tanh", false, false},
            {"log", "log", false, false},
            {"ln", "ln", false, false},
            {"exp", "exp", false, false},
            {"max", "max", false, false},
            {"min", "min", false, false},
            {"det", "det", false, false},
            {"gcd", "gcd", false, false},
            {"mod", "mod", true, false},
            {"int", "∫", false, false},
            {"iint", "∬", false, false},
            {"iiint", "∭", false, false},
            {"oint", "∮", false, false},
            {"sum", "∑", false, true},
            {"prod", "∏", false, true},
            {"coprod", "∐", false, true},
            {"lim", "lim", false, true},
            {"limsup", "lim sup", false, true},
            {"liminf", "lim inf", false, true},
            {"sup", "sup", false, true},
            {"inf", "inf", false, true},
            {"argmax", "arg max", false, true},
            {"argmin", "arg min", false, true},
            {"langle", "⟨", true, false},
            {"rangle", "⟩", true, false},
            {"lfloor", "⌊", true, false},
            {"rfloor", "⌋", true, false},
            {"lceil", "⌈", true, false},
            {"rceil", "⌉", true, false},
            {"vert", "|", true, false},
            {"Vert", "‖", true, false},
            {"mid", "|", true, false},
            {"%", "%", true, false},
            {"#", "#", true, false},
            {"&", "&amp;", true, false},
            {"_", "_", true, false},
        };
        for (const auto& symbol : symbols) {
            size_t n = std::strlen(symbol.name);
            if (static_cast<size_t>(p - nameStart) != n || std::memcmp(nameStart, symbol.name, n) != 0)
                continue;
            bool large = symbol.large;
            if (symbol.operatorLike) {
                out += "<mo>";
                out += symbol.text;
                out += "</mo>";
            } else if (large) {
                out += "<mo>";
                out += symbol.text;
                out += "</mo>";
            } else if (symbol.text[0] >= 'a' && symbol.text[0] <= 'z' && std::strlen(symbol.text) > 1) {
                out += "<mi>";
                out += symbol.text;
                out += "</mi>";
            } else if (std::strlen(symbol.text) == 2 && static_cast<unsigned char>(symbol.text[0]) >= 0x80) {
                out += "<mi>";
                out += symbol.text;
                out += "</mi>";
            } else {
                out += "<mi>";
                out += symbol.text;
                out += "</mi>";
            }
            return Scripts(start, large);
        }
        ok = false;
        return false;
    }

    bool Environment(const std::string& env, size_t start) {
        bool matrix = env == "matrix" || env == "pmatrix" || env == "bmatrix" || env == "Bmatrix" || env == "vmatrix" ||
                      env == "Vmatrix" || env == "smallmatrix" || env == "array";
        // `cases` needs a stretchy left brace and a two-column layout. Emitting
        // it as a plain aligned table produced silently wrong output, so leave
        // it to the exact C# path until the fence is implemented here.
        bool aligned = env == "aligned" || env == "align" || env == "alignedat" || env == "gathered";
        if ((!matrix && !aligned) || env == "array" || env == "alignedat" || env == "cases" || env == "dcases" ||
            env == "rcases" || env == "drcases") {
            ok = false;
            return false;
        }
        if (env == "pmatrix")
            out += "<mo>(</mo>";
        else if (env == "bmatrix")
            out += "<mo>[</mo>";
        else if (env == "Bmatrix")
            out += "<mo>{</mo>";
        else if (env == "vmatrix")
            out += "<mo>|</mo>";
        else if (env == "Vmatrix")
            out += "<mo>‖</mo>";
        out += aligned ? "<mtable columnalign=\"left\" columnspacing=\"0em\">" : "<mtable>";
        Space();
        while (p < end && ok) {
            out += "<mtr>";
            while (p < end && ok) {
                out += "<mtd>";
                if (!Row('\0'))
                    return false;
                out += "</mtd>";
                Space();
                if (Take('&'))
                    continue;
                break;
            }
            out += "</mtr>";
            Space();
            if (Take("\\\\")) {
                Space();
                continue;
            }
            if (Starts("\\end"))
                break;
            if (p >= end) {
                ok = false;
                return false;
            }
            // Tolerate whitespace/line breaks between rows.
            if (*p == '}')
                break;
            ok = false;
            return false;
        }
        if (!Take("\\end")) {
            ok = false;
            return false;
        }
        std::string closing;
        if (!GroupText(closing) || closing != env) {
            ok = false;
            return false;
        }
        out += "</mtable>";
        if (env == "pmatrix")
            out += "<mo>)</mo>";
        else if (env == "bmatrix")
            out += "<mo>]</mo>";
        else if (env == "Bmatrix")
            out += "<mo>}</mo>";
        else if (env == "vmatrix")
            out += "<mo>|</mo>";
        else if (env == "Vmatrix")
            out += "<mo>‖</mo>";
        return Scripts(start, false);
    }
};

thread_local std::string g_output;

int Render(const char* input, int length, std::string& output, bool display) {
    if (length < 0 || length > kMaxInput)
        return -2;
    output.clear();
    output.reserve(static_cast<size_t>(length) * 8 + 64);
    Parser parser(input, input + length, output, display);
    output += "<math xmlns=\"http://www.w3.org/1998/Math/MathML\" display=\"";
    output += display ? "block" : "inline";
    output += "\">";
    if (!parser.Row('\0')) {
        output.clear();
        return -1;
    }
    output += "</math>";
    return static_cast<int>(output.size());
}
} // namespace

VT_EXPORT int vt_mathml(const char* input, int length, char* output, int capacity) {
    if (!input || length < 0 || !output || capacity < 0)
        return -1;
    int size = Render(input, length, g_output, true);
    if (size < 0)
        return size;
    if (size > capacity)
        return size;
    std::memcpy(output, g_output.data(), static_cast<size_t>(size));
    return size;
}

VT_EXPORT int vt_mathml2(const char* input, int length, char* output, int capacity, int display) {
    if (!input || length < 0 || !output || capacity < 0)
        return -1;
    int size = Render(input, length, g_output, display != 0);
    if (size < 0)
        return size;
    if (size > capacity)
        return size;
    std::memcpy(output, g_output.data(), static_cast<size_t>(size));
    return size;
}

VT_EXPORT int vt_mathml_supported(const char* input, int length) {
    return Render(input, length, g_output, true) >= 0 ? 1 : 0;
}

VT_EXPORT const char* vt_version() {
    return "velocitex-fastmathml-1";
}
