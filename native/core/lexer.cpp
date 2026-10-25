#include "parser.hpp"

#include <algorithm>

namespace velocitex {
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
std::u16string_view ToScalar(int scalar, char16_t* buffer) {
    if (scalar <= 0xffff) {
        buffer[0] = (char16_t)scalar;
        return std::u16string_view(buffer, 1);
    }
    scalar -= 0x10000;
    buffer[0] = char16_t(0xd800 + (scalar >> 10));
    buffer[1] = char16_t(0xdc00 + (scalar & 1023));
    return std::u16string_view(buffer, 2);
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
                // KaTeX raises this error without a token (`src/Lexer.ts`), so
                // the error carries no position and no context.
                MathWarning warning{u"commentAtEnd", range,
                                     u"% comment has no terminating newline; LaTeX would fail because of commenting "
                                     u"the end of math mode (e.g. $)"};
                auto behavior = strictHandler ? strictHandler(warning) : strictMode;
                if (behavior == MathStrictMode::Error)
                    fail(MathDiagnosticCode::StrictViolation, SourceRange{-1, 0},
                         "LaTeX-incompatible input and strict mode is set to 'error': % comment has no terminating "
                         "newline; LaTeX would fail because of commenting the end of math mode (e.g. $) [commentAtEnd]");
                if (behavior == MathStrictMode::Warn && warnings && (int)warnings->size() < std::min(maxTokens, 1024))
                    warnings->push_back(warning);
            }
            continue;
        }
        if (c == u'\\' && std::u16string_view(source).substr(start).starts_with(u"\\verb"_u16) && start + 5 < n &&
            !Letter(source[start + 5])) {
            int delimiter = start + 5;
            if (source[delimiter] == u'*')
                delimiter++;
            if (delimiter >= n)
                // KaTeX cannot match the two delimiters of a `\verb` and
                // reports the same error as an unterminated one
                // (`src/functions/verb.ts`).
                fail(MathDiagnosticCode::UnclosedGroup, SourceRange{-1, 0},
                     "\\verb ended by end of line instead of matching delimiter");
            int end = delimiter + 1;
            while (end < n && source[end] != source[delimiter] && source[end] != u'\r' && source[end] != u'\n')
                end++;
            if (end == n || source[end] != source[delimiter])
                fail(MathDiagnosticCode::UnclosedGroup, SourceRange{-1, 0},
                     "\\verb ended by end of line instead of matching delimiter");
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
                auto wordEnd = i;
                // KaTeX's lexer attaches the whitespace that follows a
                // control word to the location of the token
                // (`src/Lexer.ts`, `controlWordWhitespaceRegexString`), so the
                // error ranges have to cover it as well.
                auto originEnd = wordEnd;
                while (originEnd < n && (source[originEnd] == u' ' || source[originEnd] == u'\r' ||
                                         source[originEnd] == u'\n' || source[originEnd] == u'\t'))
                    originEnd++;
                while (i < n && IsWhiteSpace(source[i]))
                    i++;
                auto range = origin.value_or(SourceRange{start, originEnd - start});
                if ((int)output.size() >= maxTokens)
                    fail(MathDiagnosticCode::TokenLimit, range, "Token budget exceeded.");
                output.emplace_back(owned, start, wordEnd - start, range);
                continue;
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
