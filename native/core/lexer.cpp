#include "parser.hpp"

#include <algorithm>

namespace velocitex {
bool IsWhiteSpace(char16_t c) {
    return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
           c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
static bool Letter(char16_t c) {
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == u'@';
}
int RuneAt(std::u16string_view text, int position) {
    int c = text.at(position);
    if (c >= 0xd800 && c <= 0xdbff && position + 1 < (int)text.size() && text[position + 1] >= 0xdc00 &&
        text[position + 1] <= 0xdfff)
        return 0x10000 + ((c - 0xd800) << 10) + (text[position + 1] - 0xdc00);
    return c;
}
int RuneLength(int scalar) {
    return scalar > 0xffff ? 2 : 1;
}
std::u16string FromScalar(int scalar) {
    if (scalar <= 0xffff)
        return std::u16string(1, (char16_t)scalar);
    scalar -= 0x10000;
    return {char16_t(0xd800 + (scalar >> 10)), char16_t(0xdc00 + (scalar & 1023))};
}
void Tokenize(const std::u16string& source, std::vector<LexToken>& output, int maxTokens,
              const CancellationToken& cancellationToken, std::optional<SourceRange> origin, MathStrictMode strictMode,
              std::vector<MathWarning>* warnings,
              const std::function<MathStrictMode(const MathWarning&)>& strictHandler) {
    auto owned = std::make_shared<const std::u16string>(source);
    int n = (int)source.size();
    auto fail = [&](MathDiagnosticCode code, SourceRange range, const char* message) {
        throw CompileFailure({code, range, utf16(message)});
    };
    for (int i = 0; i < n;) {
        cancellationToken.ThrowIfCancellationRequested();
        int start = i;
        char16_t c = source[i++];
        if (c == u'%') {
            while (i < n && source[i] != u'\n')
                i++;
            if (i < n)
                i++;
            else {
                auto range = origin.value_or(SourceRange{start, n - start});
                MathWarning warning{u"commentAtEnd", range, u"Comment has no terminating newline."};
                auto behavior = strictHandler ? strictHandler(warning) : strictMode;
                if (behavior == MathStrictMode::Error)
                    fail(MathDiagnosticCode::StrictViolation, range,
                         "Unterminated comment at the end of the formula. [commentAtEnd]");
                if (behavior == MathStrictMode::Warn && warnings && (int)warnings->size() < std::min(maxTokens, 1024))
                    warnings->push_back(warning);
            }
            continue;
        }
        if (c == u'\\' && std::u16string_view(source).substr(start).starts_with(u"\\verb") && start + 5 < n &&
            !Letter(source[start + 5])) {
            int delimiter = start + 5;
            if (source[delimiter] == u'*')
                delimiter++;
            if (delimiter >= n)
                fail(MathDiagnosticCode::UnclosedGroup, origin.value_or(SourceRange{start, n - start}),
                     "Verbatim text requires a matching delimiter.");
            int end = delimiter + 1;
            while (end < n && source[end] != source[delimiter] && source[end] != u'\r' && source[end] != u'\n')
                end++;
            if (end == n || source[end] != source[delimiter])
                fail(MathDiagnosticCode::UnclosedGroup, origin.value_or(SourceRange{start, end - start}),
                     "Verbatim text ended before its matching delimiter.");
            i = end + 1;
        } else if (c == u'\\' && i < n) {
            if (IsWhiteSpace(source[i])) {
                while (i < n && (source[i] == u' ' || source[i] == u'\r' || source[i] == u'\t'))
                    i++;
                if (i < n && source[i] == u'\n')
                    i++;
                while (i < n && IsWhiteSpace(source[i]))
                    i++;
                auto range = origin.value_or(SourceRange{start, i - start});
                if ((int)output.size() >= maxTokens)
                    fail(MathDiagnosticCode::TokenLimit, range, "Token budget exceeded.");
                output.emplace_back(u"\\ ", 0, 2, range);
                continue;
            }
            if (Letter(source[i])) {
                while (i < n && Letter(source[i]))
                    i++;
            } else if (source[i] >= 0xd800 && source[i] <= 0xdbff && i + 1 < n && source[i + 1] >= 0xdc00 &&
                       source[i + 1] <= 0xdfff)
                i += 2;
            else
                i++;
        } else if (IsWhiteSpace(c)) {
            while (i < n && IsWhiteSpace(source[i]))
                i++;
        } else if (c >= 0xd800 && c <= 0xdfff) {
            if (c <= 0xdbff && i < n && source[i] >= 0xdc00 && source[i] <= 0xdfff)
                i++;
            else
                fail(MathDiagnosticCode::UnknownSymbol, origin.value_or(SourceRange{start, 1}),
                     "Unpaired UTF-16 surrogate.");
        }
        if (c != u'\\' && !IsWhiteSpace(c))
            while (i < n && source[i] >= 0x300 && source[i] <= 0x36f)
                i++;
        auto range = origin.value_or(SourceRange{start, i - start});
        if ((int)output.size() >= maxTokens)
            fail(MathDiagnosticCode::TokenLimit, range, "Token budget exceeded.");
        output.emplace_back(owned, start, i - start, range);
        if (c == u'\\' && i > start + 1 && Letter(source[start + 1]))
            while (i < n && IsWhiteSpace(source[i]))
                i++;
    }
}
} // namespace velocitex
