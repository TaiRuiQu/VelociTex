// KaTeX-compatible WebAssembly entry points for the standalone C++ core.
//
// The module has no JavaScript imports: the browser adapter allocates a UTF-8
// input buffer, calls `vt_render`, and reads the returned bytes before
// `vt_free_result`.  The payload starts with `O` for a rendered document or
// `E` for a JSON diagnostic (which also carries the KaTeX error markup when
// `throwOnError` is false).
//
// The markup is produced by the very same core the .NET endpoint uses: the
// semantic tree is built once and then serialized to KaTeX-compatible HTML
// and MathML.  Nothing in this file re-implements TeX parsing.

#include "core.hpp"
#include "state.hpp"
#include "mathml.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

using velocitex::MathDiagnostic;
using velocitex::MathDiagnosticCode;
using velocitex::MathDocument;
using velocitex::MathOptions;
using velocitex::MathStrictMode;
using velocitex::MathWorkspace;

// ------------------------------------------------------------------ text --

// Writes the UTF-16 of a text into a buffer that the caller owns.
//
// The buffer is reused between renders, so a formula does not ask the heap
// for a string of its own.
void Utf8ToUtf16Into(std::u16string& out, std::string_view text);

std::u16string Utf8ToUtf16(std::string_view text) {
    std::u16string out;
    Utf8ToUtf16Into(out, text);
    return out;
}

void Utf8ToUtf16Into(std::u16string& out, std::string_view text) {
    out.clear();
    // Every byte of the text is at most one code unit, so the buffer can hold
    // the whole text.  It is written directly, without the capacity check of
    // `push_back` for every character, and trimmed to the length that was
    // really written at the end.
    out.resize(text.size());
    char16_t* dst = out.data();
    const unsigned char* src = reinterpret_cast<const unsigned char*>(text.data());
    const size_t size = text.size();
    size_t i = 0;
    size_t written = 0;
    while (i < size) {
        const unsigned char c = src[i];
        if (c < 0x80) {
            // A run of ASCII is the common case of a formula.
            size_t j = i + 1;
            while (j < size && src[j] < 0x80) {
                ++j;
            }
            for (size_t k = i; k < j; ++k) {
                dst[written++] = static_cast<char16_t>(src[k]);
            }
            i = j;
            continue;
        }
        uint32_t code = 0;
        size_t extra = 0;
        if ((c & 0xE0) == 0xC0) {
            code = c & 0x1Fu;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            code = c & 0x0Fu;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            code = c & 0x07u;
            extra = 3;
        } else {
            code = 0xFFFD;
        }
        if (i + extra >= size) {
            code = 0xFFFD;
            extra = 0;
        }
        for (size_t k = 1; k <= extra; ++k) {
            const unsigned char cc = src[i + k];
            if ((cc & 0xC0) != 0x80) {
                code = 0xFFFD;
                extra = 0;
                break;
            }
            code = (code << 6) | (cc & 0x3Fu);
        }
        i += extra + 1;
        if (code >= 0x10000) {
            code -= 0x10000;
            dst[written++] = static_cast<char16_t>(0xD800 + (code >> 10));
            dst[written++] = static_cast<char16_t>(0xDC00 + (code & 0x3FF));
        } else {
            dst[written++] = static_cast<char16_t>(code);
        }
    }
    out.resize(written);
}

std::string Utf16ToUtf8(std::u16string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t code = text[i];
        if (code >= 0xD800 && code <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 &&
            text[i + 1] <= 0xDFFF) {
            code = 0x10000 + ((code - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            ++i;
        }
        if (code >= 0x10000) {
            // The four byte form encodes the whole code point.  The shifts must
            // therefore read the code point and not the part below 0x10000.
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code >= 0xD800 && code <= 0xDFFF) {
            // A lone surrogate has no UTF-8 form.  It is written in the WTF-8
            // form, which `EscapeJson` turns into the JSON escape that keeps
            // the character in the string the host reads back.
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code >= 0x800) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code >= 0x80) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(code));
        }
    }
    return out;
}

// Escapes the same characters as KaTeX's `escape` helper in `src/utils.ts`.
std::string Escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '>': out += "&gt;"; break;
        case '<': out += "&lt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#x27;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

// Escapes a string for a JSON payload.
//
// A lone surrogate has no UTF-8 form, so the encoder writes it in the
// WTF-8 form; the escape below turns it back into the JSON escape that
// `JSON.parse` turns into the lone surrogate again.  KaTeX keeps such a
// character in the JavaScript string it returns, and the two have to be the
// same string.
std::string EscapeJson(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        // The WTF-8 form of a surrogate: 0xED and two bytes of the payload.
        if (c == 0xED && i + 2 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80 &&
            (static_cast<unsigned char>(text[i + 2]) & 0xC0) == 0x80) {
            const unsigned int code = 0xD000u |
                                      ((static_cast<unsigned int>(static_cast<unsigned char>(text[i + 1])) & 0x3Fu) << 6) |
                                      (static_cast<unsigned int>(static_cast<unsigned char>(text[i + 2])) & 0x3Fu);
            if (code >= 0xD800 && code <= 0xDFFF) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", code);
                out += buffer;
                i += 3;
                continue;
            }
        }
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
        ++i;
    }
    return out;
}

// ------------------------------------------------------------------ json --

// The option object the JavaScript adapter sends is small and flat, so a
// focused reader is enough; anything unexpected is ignored exactly like a
// missing option would be.
struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Object, Array };
    Kind Type = Kind::Null;
    bool Boolean{};
    double Number{};
    std::string String;
    std::vector<std::pair<std::string, JsonValue>> Members;

    const JsonValue* Find(std::string_view name) const {
        if (Type != Kind::Object)
            return nullptr;
        for (const auto& [key, value] : Members) {
            if (key == name)
                return &value;
        }
        return nullptr;
    }
};

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text(text) {}

    bool Read(JsonValue& value) {
        SkipSpace();
        if (!ReadValue(value, 0))
            return false;
        SkipSpace();
        return position == text.size();
    }

private:
    static constexpr int kMaxDepth = 32;

    void SkipSpace() {
        while (position < text.size()) {
            char c = text[position];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++position;
            else
                break;
        }
    }

    bool ReadValue(JsonValue& value, int depth) {
        if (depth > kMaxDepth || position >= text.size())
            return false;
        switch (text[position]) {
        case '{': return ReadObject(value, depth);
        case '[': return ReadArray(value, depth);
        case '"': {
            value.Type = JsonValue::Kind::String;
            return ReadString(value.String);
        }
        case 't':
            if (text.compare(position, 4, "true") != 0)
                return false;
            position += 4;
            value.Type = JsonValue::Kind::Bool;
            value.Boolean = true;
            return true;
        case 'f':
            if (text.compare(position, 5, "false") != 0)
                return false;
            position += 5;
            value.Type = JsonValue::Kind::Bool;
            value.Boolean = false;
            return true;
        case 'n':
            if (text.compare(position, 4, "null") != 0)
                return false;
            position += 4;
            value.Type = JsonValue::Kind::Null;
            return true;
        default: return ReadNumber(value);
        }
    }

    bool ReadObject(JsonValue& value, int depth) {
        value.Type = JsonValue::Kind::Object;
        ++position; // '{'
        SkipSpace();
        if (position < text.size() && text[position] == '}') {
            ++position;
            return true;
        }
        while (true) {
            SkipSpace();
            std::string key;
            if (position >= text.size() || text[position] != '"' || !ReadString(key))
                return false;
            SkipSpace();
            if (position >= text.size() || text[position] != ':')
                return false;
            ++position;
            SkipSpace();
            JsonValue member;
            if (!ReadValue(member, depth + 1))
                return false;
            value.Members.emplace_back(std::move(key), std::move(member));
            SkipSpace();
            if (position >= text.size())
                return false;
            if (text[position] == ',') {
                ++position;
                continue;
            }
            if (text[position] == '}') {
                ++position;
                return true;
            }
            return false;
        }
    }

    bool ReadArray(JsonValue& value, int depth) {
        value.Type = JsonValue::Kind::Array;
        ++position; // '['
        SkipSpace();
        if (position < text.size() && text[position] == ']') {
            ++position;
            return true;
        }
        while (true) {
            SkipSpace();
            JsonValue member;
            if (!ReadValue(member, depth + 1))
                return false;
            value.Members.emplace_back(std::string(), std::move(member));
            SkipSpace();
            if (position >= text.size())
                return false;
            if (text[position] == ',') {
                ++position;
                continue;
            }
            if (text[position] == ']') {
                ++position;
                return true;
            }
            return false;
        }
    }

    bool ReadString(std::string& value) {
        ++position; // opening quote
        value.clear();
        while (position < text.size()) {
            char c = text[position++];
            if (c == '"')
                return true;
            if (c != '\\') {
                value.push_back(c);
                continue;
            }
            if (position >= text.size())
                return false;
            char escape = text[position++];
            switch (escape) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u': {
                if (position + 4 > text.size())
                    return false;
                uint32_t code = 0;
                for (int i = 0; i < 4; ++i) {
                    char digit = text[position++];
                    code <<= 4;
                    if (digit >= '0' && digit <= '9')
                        code |= uint32_t(digit - '0');
                    else if (digit >= 'a' && digit <= 'f')
                        code |= uint32_t(digit - 'a' + 10);
                    else if (digit >= 'A' && digit <= 'F')
                        code |= uint32_t(digit - 'A' + 10);
                    else
                        return false;
                }
                if (code >= 0xD800 && code <= 0xDBFF && position + 6 <= text.size() &&
                    text[position] == '\\' && text[position + 1] == 'u') {
                    uint32_t low = 0;
                    size_t probe = position + 2;
                    bool ok = true;
                    for (int i = 0; i < 4; ++i) {
                        char digit = text[probe++];
                        low <<= 4;
                        if (digit >= '0' && digit <= '9')
                            low |= uint32_t(digit - '0');
                        else if (digit >= 'a' && digit <= 'f')
                            low |= uint32_t(digit - 'a' + 10);
                        else if (digit >= 'A' && digit <= 'F')
                            low |= uint32_t(digit - 'A' + 10);
                        else {
                            ok = false;
                            break;
                        }
                    }
                    if (ok && low >= 0xDC00 && low <= 0xDFFF) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        position = probe;
                    }
                }
                // Encode as UTF-8.
                if (code >= 0x10000) {
                    // The four byte form encodes the whole code point, so the
                    // shifts must read the code point and not the part below
                    // 0x10000.
                    value.push_back(static_cast<char>(0xF0 | (code >> 18)));
                    value.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                    value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else if (code >= 0x800) {
                    value.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    value.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else {
                    value.push_back(static_cast<char>(code));
                }
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool ReadNumber(JsonValue& value) {
        size_t start = position;
        if (position < text.size() && (text[position] == '-' || text[position] == '+'))
            ++position;
        while (position < text.size()) {
            char c = text[position];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+')
                ++position;
            else
                break;
        }
        if (position == start)
            return false;
        value.Type = JsonValue::Kind::Number;
        value.Number = std::strtod(std::string(text.substr(start, position - start)).c_str(), nullptr);
        return true;
    }

    std::string_view text;
    size_t position = 0;
};

// --------------------------------------------------------------- options --

struct RenderSettings {
    bool DisplayMode{};
    bool ThrowOnError{true};
    bool Leqno{}, Fleqn{}, ColorIsTextColor{}, GlobalGroup{};
    double MinRuleThickness{0.04};
    double MaxSize{std::numeric_limits<double>::infinity()};
    int MaxExpand{1000};
    std::string ErrorColor{"#cc0000"};
    MathStrictMode Strict{MathStrictMode::Warn};
    std::unordered_map<std::u16string, std::u16string> Macros;
};

void ReadOptions(std::string_view text, RenderSettings& settings) {
    JsonValue root;
    JsonReader reader(text);
    if (!reader.Read(root) || root.Type != JsonValue::Kind::Object)
        return;
    auto boolean = [&](const char* name, bool& target) {
        if (const JsonValue* value = root.Find(name)) {
            if (value->Type == JsonValue::Kind::Bool)
                target = value->Boolean;
        }
    };
    auto number = [&](const char* name, double& target) {
        if (const JsonValue* value = root.Find(name)) {
            if (value->Type == JsonValue::Kind::Number)
                target = value->Number;
        }
    };
    boolean("displayMode", settings.DisplayMode);
    boolean("throwOnError", settings.ThrowOnError);
    boolean("leqno", settings.Leqno);
    boolean("fleqn", settings.Fleqn);
    boolean("colorIsTextColor", settings.ColorIsTextColor);
    boolean("globalGroup", settings.GlobalGroup);
    number("minRuleThickness", settings.MinRuleThickness);
    number("maxSize", settings.MaxSize);
    if (const JsonValue* value = root.Find("maxExpand")) {
        if (value->Type == JsonValue::Kind::Number)
            settings.MaxExpand = int(value->Number);
    }
    if (const JsonValue* value = root.Find("errorColor")) {
        if (value->Type == JsonValue::Kind::String)
            settings.ErrorColor = value->String;
    }
    if (const JsonValue* value = root.Find("strict")) {
        if (value->Type == JsonValue::Kind::Bool)
            settings.Strict = value->Boolean ? MathStrictMode::Error : MathStrictMode::Ignore;
        else if (value->Type == JsonValue::Kind::String) {
            if (value->String == "ignore")
                settings.Strict = MathStrictMode::Ignore;
            else if (value->String == "error")
                settings.Strict = MathStrictMode::Error;
            else
                settings.Strict = MathStrictMode::Warn;
        }
    }
    if (const JsonValue* value = root.Find("macros")) {
        if (value->Type == JsonValue::Kind::Object) {
            for (const auto& [name, body] : value->Members) {
                if (body.Type != JsonValue::Kind::String)
                    continue;
                settings.Macros[Utf8ToUtf16(name)] = Utf8ToUtf16(body.String);
            }
        }
    }
}

// --------------------------------------------------------------- markup --

const char* DiagnosticCode(MathDiagnosticCode code) {
    switch (code) {
    case MathDiagnosticCode::None: return "None";
    case MathDiagnosticCode::InvalidOptions: return "InvalidOptions";
    case MathDiagnosticCode::InputLimit: return "InputLimit";
    case MathDiagnosticCode::NodeLimit: return "NodeLimit";
    case MathDiagnosticCode::NestingLimit: return "NestingLimit";
    case MathDiagnosticCode::ExpansionLimit: return "ExpansionLimit";
    case MathDiagnosticCode::TokenLimit: return "TokenLimit";
    case MathDiagnosticCode::UnexpectedToken: return "UnexpectedToken";
    case MathDiagnosticCode::MissingArgument: return "MissingArgument";
    case MathDiagnosticCode::UnclosedGroup: return "UnclosedGroup";
    case MathDiagnosticCode::UnexpectedGroupEnd: return "UnexpectedGroupEnd";
    case MathDiagnosticCode::DoubleSuperscript: return "DoubleSuperscript";
    case MathDiagnosticCode::DoubleSubscript: return "DoubleSubscript";
    case MathDiagnosticCode::UnsupportedCommand: return "UnsupportedCommand";
    case MathDiagnosticCode::UnknownSymbol: return "UnknownSymbol";
    case MathDiagnosticCode::InvalidMacroDefinition: return "InvalidMacroDefinition";
    case MathDiagnosticCode::WorkspaceInUse: return "WorkspaceInUse";
    case MathDiagnosticCode::SessionInUse: return "SessionInUse";
    case MathDiagnosticCode::Cancelled: return "Cancelled";
    case MathDiagnosticCode::StrictViolation: return "StrictViolation";
    }
    return "UnexpectedToken";
}

// Rebuilds KaTeX's `ParseError.toString()` shape so that the `title`
// attribute of the error markup reads like KaTeX's own message.
std::string ErrorTitle(std::u16string_view source, const MathDiagnostic& diagnostic) {
    std::string message = "KaTeX parse error: " + Utf16ToUtf8(diagnostic.Message);
    int start = diagnostic.Range.Start;
    int length = diagnostic.Range.Length;
    // A negative start marks an error that KaTeX raises without a token.
    // Such an error carries no position and no context.
    if (start < 0)
        return "ParseError: " + message;
    if (length < 0)
        length = 0;
    int end = start + length;
    if (start <= end && size_t(end) <= source.size()) {
        if (size_t(start) == source.size()) {
            message += " at end of input: ";
        } else {
            message += " at position " + std::to_string(start + 1) + ": ";
        }
        std::string underlined;
        for (int i = start; i < end; ++i) {
            underlined += Utf16ToUtf8(source.substr(size_t(i), 1));
            underlined += "\xCC\xB2"; // combining low line (U+0332)
        }
        std::string left;
        if (start > 15) {
            left = "\xE2\x80\xA6" + Utf16ToUtf8(source.substr(size_t(start - 15), 15));
        } else {
            left = Utf16ToUtf8(source.substr(0, size_t(start)));
        }
        std::string right;
        if (size_t(end + 15) < source.size()) {
            right = Utf16ToUtf8(source.substr(size_t(end), 15)) + "\xE2\x80\xA6";
        } else {
            right = Utf16ToUtf8(source.substr(size_t(end)));
        }
        message += left + underlined + right;
    }
    return "ParseError: " + message;
}

// KaTeX's `renderError` markup for a formula that could not be parsed.
//
// KaTeX renders the source of the formula as a single symbol node
// (`src/katex.ts`, `renderError`), so a source that starts with a character of
// a script that KaTeX does not have metrics for carries the
// `<script>_fallback` class.
std::string ErrorMarkup(std::u16string_view source, const MathDiagnostic& diagnostic,
                        const std::string& error_color) {
    std::string title = ErrorTitle(source, diagnostic);
    std::string body = Escape(Utf16ToUtf8(source));
    if (!source.empty()) {
        // KaTeX looks at the first code unit of the text, not at the first
        // code point (`src/domTree.ts`, `SymbolNode`).
        if (const char* script = velocitex::UnicodeScriptFamily(static_cast<int>(source[0])))
            body = "<span class=\"" + std::string(script) + "_fallback\">" + body + "</span>";
    }
    return "<span class=\"katex-error\" title=\"" + Escape(title) + "\" style=\"color:" + error_color +
           "\">" + body + "</span>";
}

// Appends the markup to `out` so that the payload never needs an extra copy.
void AssembleMarkup(velocitex::html::MarkupBuffer& out, MathDocument& document, const RenderSettings& settings,
                     uint32_t output) {
    if (output == 2) {
        // KaTeX returns the MathML-only output without the display wrapper.
        out += "<span class=\"katex\">";
        velocitex::document_mathml_into(document, out, true);
        out += "</span>";
        return;
    }
    if (settings.DisplayMode) {
        std::string classes = "katex-display";
        if (settings.Leqno)
            classes += " leqno";
        if (settings.Fleqn)
            classes += " fleqn";
        out += "<span class=\"" + classes + "\">";
    }
    if (output == 1) {
        out += "<span class=\"katex\">";
        velocitex::document_html_into(document, out);
        out += "</span>";
    } else {
        out += "<span class=\"katex\"><span class=\"katex-mathml\">";
        velocitex::document_mathml_into(document, out, true);
        out += "</span>";
        velocitex::document_html_into(document, out);
        out += "</span>";
    }
    if (settings.DisplayMode)
        out += "</span>";
}

std::string ErrorPayload(std::u16string_view source, const MathDiagnostic& diagnostic,
                          const RenderSettings& settings) {
    std::string payload = "{\"code\":\"" + std::string(DiagnosticCode(diagnostic.Code)) + "\",\"message\":\"" +
                          EscapeJson(Utf16ToUtf8(diagnostic.Message)) + "\",\"start\":" +
                          std::to_string(diagnostic.Range.Start) + ",\"length\":" +
                          std::to_string(diagnostic.Range.Length);
    if (!settings.ThrowOnError) {
        payload += ",\"html\":\"" + EscapeJson(ErrorMarkup(source, diagnostic, settings.ErrorColor)) + "\"";
    }
    payload += "}";
    return payload;
}

// The options that travel with every formula.  The JSON is parsed once and
// the result is reused while the options do not change, so a document of
// formulas parses the options exactly once.
struct CachedOptions {
    std::string Json;
    RenderSettings Settings;
    velocitex::MathOptions Options;
};

// The state that the module reuses between renders.
//
// The state lives in a namespace scope object: a `thread_local` carries an
// initialization guard on every access, and the guard is a call into the
// runtime.  The WebAssembly module is single threaded, so the state does not
// have to be per thread, and a namespace scope object is initialized before
// the module runs and costs nothing to reach.
struct RenderState {
    CachedOptions Cached;
    std::u16string Source;
    velocitex::MathWorkspace Workspace;
    std::unique_ptr<MathDocument> Document;
};
VELOCITEX_REUSED_STATE(RenderState, g_render_state);

void Render(velocitex::html::MarkupBuffer& out, std::string_view source_utf8, std::string_view options_json,
            uint32_t output) {
    // The options travel with every formula, so the JSON is parsed once and
    // the result is reused while the options do not change.  A document of
    // formulas therefore parses the options exactly once.
    RenderState& state = g_render_state;
    CachedOptions& cached = state.Cached;
    if (cached.Json != options_json) {
        cached.Json.assign(options_json);
        // The settings of the previous render are dropped first: the reader
        // only adds the macros it finds, and the macros of a document must not
        // leak into the next one.
        cached.Settings = RenderSettings{};
        ReadOptions(options_json, cached.Settings);
        const RenderSettings& settings = cached.Settings;
        velocitex::MathOptions& options = cached.Options;
        options.DisplayMode = settings.DisplayMode;
        options.Leqno = settings.Leqno;
        options.Fleqn = settings.Fleqn;
        options.ColorIsTextColor = settings.ColorIsTextColor;
        options.GlobalGroup = settings.GlobalGroup;
        options.MinRuleThickness = settings.MinRuleThickness;
        options.MaxSize = settings.MaxSize;
        options.MaxExpansion = settings.MaxExpand;
        options.StrictMode = settings.Strict;
        options.ThrowOnError = settings.ThrowOnError;
        // The macro table is only read while the formula is parsed, so the
        // parsed table can be kept instead of copied for every formula.
        options.Macros = settings.Macros;
        options.ErrorColor = Utf8ToUtf16(settings.ErrorColor);
    }
    const RenderSettings& settings = cached.Settings;

    // The buffer of the source is reused between renders, so a formula does
    // not ask the heap for a string of its own.
    std::u16string& source = state.Source;
    Utf8ToUtf16Into(source, source_utf8);

    // The workspace and the document are kept between renders.  The parser
    // reuses the buffers of the workspace, and the document reuses its node
    // arena and its cached markup, which removes a large number of
    // allocations per formula.
    MathWorkspace& workspace = state.Workspace;
    std::unique_ptr<MathDocument>& document = state.Document;
    MathDiagnostic diagnostic;
    // The HTML and MathML outputs never need the drawing layout, so the core
    // skips it entirely; that is where most of the speed advantage comes from.
    if (!velocitex::try_compile(source, cached.Options, workspace, document, diagnostic, false)) {
        document.reset();
        out += "E";
        out += ErrorPayload(source, diagnostic, settings);
        return;
    }
    out += "O";
    AssembleMarkup(out, *document, settings, output);
}

// The result buffer is owned by the module until the host frees it.  Keeping
// the payload in one reusable buffer avoids a copy per render; the host reads
// the bytes before it asks for the next document.  The markup is written into
// the buffer directly, so the payload never needs a second copy either.
velocitex::html::MarkupBuffer g_result;

}  // namespace

extern "C" {

// Allocates a buffer the host can write UTF-8 bytes into.
void* vt_alloc(size_t length) {
    if (length == 0)
        length = 1;
    return std::malloc(length);
}

// Releases a buffer obtained from `vt_alloc`.
void vt_free(void* pointer, size_t capacity) {
    (void)capacity;
    std::free(pointer);
}

// Returns the length of the last `vt_render` result.
size_t vt_result_len() {
    return g_result.size();
}

// Releases the buffer returned by `vt_render`.  The storage stays allocated
// so that the next render can reuse it.
void vt_free_result(void* pointer) {
    (void)pointer;
    g_result.Clear();
}

// Renders a UTF-8 formula. `output` is 0 for KaTeX's htmlAndMathml, 1 for
// HTML-only and 2 for MathML-only.
void* vt_render(const unsigned char* input, size_t input_length, const unsigned char* options, size_t options_length,
                uint32_t output) {
    if (!input)
        return nullptr;
    const std::string_view source(reinterpret_cast<const char*>(input), input_length);
    const std::string_view option_text(options && options_length
                                           ? reinterpret_cast<const char*>(options)
                                           : "",
                                       options ? options_length : 0);
    g_result.Clear();
    try {
        Render(g_result, source, option_text, output);
    } catch (...) {
        g_result.Assign("E{\"code\":\"UnexpectedToken\",\"message\":\"Unable to render formula.\",\"start\":0,\"length\":0}");
    }
    return g_result.empty() ? nullptr : const_cast<char*>(g_result.data());
}

// Returns the ABI revision of the module.
uint32_t vt_version() {
    return 2;
}

}  // extern "C"
