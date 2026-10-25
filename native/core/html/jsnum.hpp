// JavaScript-compatible number formatting helpers.
//
// KaTeX formats CSS lengths with `+(n.toFixed(4)) + "em"`, and the resulting
// strings are compared byte-for-byte during acceptance.  These helpers
// reproduce the JavaScript semantics exactly.
//
// The helpers deliberately avoid `std::to_chars` for floating point: the
// libstdc++ implementation shipped with GCC 11 mis-rounds doubles when a
// precision is requested, and `snprintf` is too slow for a routine that runs
// a few dozen times per formula.
#pragma once

#include <algorithm>
#include <bit>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>

#include "markup.hpp"

namespace velocitex::html {

// True when the current locale reads numbers with a decimal point.
//
// `strtod` follows the separator of the locale, and the number that is read
// has to be the one that `printf` wrote.  Both hosts of the library run in
// the "C" locale; a program that installs another one would read a wrong
// number, and the caller falls back to the long form then.
inline bool UsesDecimalPoint() {
    const std::lconv* conventions = std::localeconv();
    return conventions != nullptr && conventions->decimal_point != nullptr &&
           conventions->decimal_point[0] == '.' && conventions->decimal_point[1] == '\0';
}

// Reads the decimal number in `[first, last)`.
//
// The library of the WebAssembly build does not offer the floating point
// overload of `std::from_chars`, so the number is read with `strtod`.
inline bool ParseDecimal(const char* first, const char* last, double& out) {
    char* end = nullptr;
    const double parsed = std::strtod(first, &end);
    if (end != last) {
        return false;
    }
    out = parsed;
    return true;
}

// JavaScript's `String(number)`: the shortest round-trip representation,
// formatted the way the ECMAScript `Number::toString` algorithm does.
inline std::string JsNumberToString(double value) {
    if (std::isnan(value)) {
        return "NaN";
    }
    if (value == 0.0) {
        return "0";
    }
    if (std::isinf(value)) {
        return value < 0 ? "-Infinity" : "Infinity";
    }
    // Fast path for an integral value.  Every magnitude below 2^53 is an exact
    // integer, and the shortest representation that round-trips to such a
    // double is the integer itself, so the digits can be written directly.
    // That skips the `printf` and the `strtod` round trip below, which the
    // SVG paths of the stretchy elements pay for every coordinate.
    if (value == std::trunc(value) && std::fabs(value) < 9007199254740992.0) {
        uint64_t magnitude = static_cast<uint64_t>(std::fabs(value));
        char digits[24];
        size_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + magnitude % 10);
            magnitude /= 10;
        } while (magnitude != 0);
        std::string out;
        if (value < 0.0) {
            out.push_back('-');
        }
        while (count > 0) {
            out.push_back(digits[--count]);
        }
        return out;
    }
    // The shortest representation that still parses back to the same double is
    // the one with the fewest significant digits; `printf` rounds to the
    // nearest digit and breaks ties to even, which is what the algorithm
    // requires.
    char buffer[64];
    if (UsesDecimalPoint()) {
        for (int precision = 0; precision < 17; ++precision) {
            std::snprintf(buffer, sizeof(buffer), "%.*e", precision, value);
            double parsed = 0.0;
            const char* first = buffer;
            const char* last = buffer + std::strlen(buffer);
            if (ParseDecimal(first, last, parsed) && parsed == value) {
                break;
            }
        }
    } else {
        // A locale with another decimal separator would break the round trip.
        // Seventeen digits always name the same double, so the long form is
        // used; the trailing zeros are dropped further down.
        std::snprintf(buffer, sizeof(buffer), "%.16e", value);
    }
    // The scientific form is "-d.dddde+XX".
    const char* p = buffer;
    bool negative = false;
    if (*p == '-') {
        negative = true;
        ++p;
    }
    std::string digits;
    for (; *p && *p != 'e' && *p != 'E'; ++p) {
        if (*p != '.') {
            digits.push_back(*p);
        }
    }
    const int exponent = (*p == 'e' || *p == 'E') ? std::atoi(p + 1) : 0;
    while (digits.size() > 1 && digits.back() == '0') {
        digits.pop_back();
    }
    const int count = static_cast<int>(digits.size());
    // The value is 0.<digits> times ten to the power `point`.
    const int point = exponent + 1;
    std::string out;
    if (negative) {
        out.push_back('-');
    }
    if (count <= point && point <= 21) {
        out += digits;
        out.append(static_cast<size_t>(point - count), '0');
    } else if (0 < point && point <= 21) {
        out.append(digits, 0, static_cast<size_t>(point));
        out.push_back('.');
        out.append(digits, static_cast<size_t>(point), std::string::npos);
    } else if (-6 < point && point <= 0) {
        out += "0.";
        out.append(static_cast<size_t>(-point), '0');
        out += digits;
    } else {
        out.push_back(digits[0]);
        if (count > 1) {
            out.push_back('.');
            out.append(digits, 1, std::string::npos);
        }
        out.push_back('e');
        const int e = point - 1;
        out.push_back(e < 0 ? '-' : '+');
        out += std::to_string(e < 0 ? -e : e);
    }
    return out;
}

// Rounds the exact binary value of `magnitude` to four decimal places and
// returns the result scaled by ten thousand.  The rounding is the one
// JavaScript's `toFixed` performs: half away from zero, applied to the exact
// value of the double rather than to a rounded intermediate product.
//
// `exact` reports whether the scaled result fits into 64 bits; magnitudes
// that large are not CSS lengths and take the slow path instead.
inline uint64_t ScaleByTenThousand(double magnitude, bool& exact) {
    exact = true;
    if (magnitude == 0.0) {
        return 0;
    }
    // The scaled result must stay below 2^64, so the magnitude is limited to
    // 2^49.
    if (magnitude >= 562949953421312.0) {
        exact = false;
        return 0;
    }
    const uint64_t bits = std::bit_cast<uint64_t>(magnitude);
    const int biased = static_cast<int>((bits >> 52) & 0x7FFull);
    uint64_t mantissa = bits & 0xFFFFFFFFFFFFFull;
    int exponent = -1074;
    if (biased != 0) {
        mantissa |= 1ull << 52;
        exponent = biased - 1075;
    }
    if (exponent >= 0) {
        // The value is an integer, so the scaled result is exact as well.
        const uint64_t integral = mantissa << exponent;
        return integral * 10000ull;
    }
    // The value is `mantissa * 2^exponent`; scaling it by ten thousand is
    // `mantissa * 625 * 16 / 2^k`, which keeps every intermediate below
    // 2^63.
    const int k = -exponent;
    const uint64_t a = mantissa * 625ull;
    if (k <= 4) {
        return a << (4 - k);
    }
    const int s = k - 4;
    if (s >= 64) {
        // The value is below 2^-15 and rounds to zero.
        return 0;
    }
    const uint64_t quotient = a >> s;
    const uint64_t remainder = a & ((1ull << s) - 1ull);
    const uint64_t half = 1ull << (s - 1);
    // A remainder of exactly one half is a tie and rounds away from zero.
    return quotient + (remainder >= half ? 1ull : 0ull);
}

// A short text on the stack.
//
// The length of an attribute is formatted into it, so that the formatting
// does not ask the string library for a string of its own.  In WebAssembly
// every call into the library costs more than the formatting itself.
class StackText {
public:
    void operator+=(const char* text) {
        const size_t length = std::strlen(text);
        Append(text, length);
    }
    void operator+=(const std::string& text) { Append(text.data(), text.size()); }
    void Append(const char* text, size_t length) {
        // The longest text that is written here is a JavaScript number with
        // its "em" suffix, so the buffer is always big enough.
        if (length_ + length > sizeof(data_)) {
            return;
        }
        std::memcpy(data_ + length_, text, length);
        length_ += length;
    }
    void append(const char* text, size_t length) { Append(text, length); }
    void append(const char* text, size_t start, size_t length) {
        Append(text + start, length);
    }
    void append(std::string_view text, size_t start, size_t length) {
        Append(text.data() + start, length);
    }
    void append(std::string_view text, size_t length) { Append(text.data(), length); }
    void push_back(char c) { Append(&c, 1); }
    std::string_view View() const { return std::string_view(data_, length_); }

private:
    char data_[64];
    size_t length_ = 0;
};

// Equivalent of KaTeX's `makeEm`: `+(n.toFixed(4)) + "em"`.
//
// JavaScript's `toFixed` rounds the *exact* value of the double to the
// requested number of decimals and breaks ties away from zero.  Multiplying
// by a power of ten first (as `std::round(n * 10000.0)` would) rounds the
// already rounded product instead, so 0.46375 would become 0.4638 instead of
// JavaScript's 0.4637.
//
// The unary `+` in KaTeX's expression then parses the rounded decimal again
// and prints it with `String(number)`, which is the shortest representation
// that round-trips.  For the magnitudes that occur here that is the rounded
// decimal with its trailing zeros removed, and it is reproduced directly; only
// magnitudes with more than fifteen significant digits need the round trip.
//
// The result is appended to `out` so that the caller does not need a
// temporary string.
// The buffer is a template parameter so that the same code can write into a
// `std::string` and into the markup buffer.
template <typename Buffer>
[[gnu::noinline]] inline void AppendEmRare(Buffer& out, double n) {
    if (std::isnan(n)) {
        out += "NaNem";
        return;
    }
    if (std::isinf(n)) {
        out += JsNumberToString(n);
        out += "em";
        return;
    }
    const bool negative = n < 0.0;
    const double magnitude = std::fabs(n);
    if (magnitude >= 1e21) {
        // JavaScript's `toFixed` falls back to the exponential form here.
        out += JsNumberToString(n);
        out += "em";
        return;
    }
    bool exact = false;
    const uint64_t scaled = ScaleByTenThousand(magnitude, exact);
    // The integer part is written into a small buffer; going through
    // std::to_string would allocate a string for every length.
    char whole_buffer[24];
    size_t whole_length = 0;
    char fraction[4] = {'0', '0', '0', '0'};
    if (exact) {
        // One division instead of two: the remainder of the division by ten
        // thousand is the fraction.  A 64 bit division is expensive, and in
        // WebAssembly it is a real instruction rather than a multiplication.
        if (scaled <= 0xFFFFFFFFull) {
            // The scaled value fits into 32 bits.  The division by a constant
            // becomes a multiplication with a high part, and WebAssembly has
            // no instruction for the 64 bit variant, so the narrow form is
            // much cheaper there.
            const uint32_t quotient = static_cast<uint32_t>(scaled) / 10000u;
            uint32_t rest = static_cast<uint32_t>(scaled) - quotient * 10000u;
            for (int i = 3; i >= 0; --i) {
                fraction[i] = static_cast<char>('0' + rest % 10u);
                rest /= 10u;
            }
            uint32_t integer = quotient;
            char reversed[24];
            size_t count = 0;
            do {
                reversed[count++] = static_cast<char>('0' + integer % 10u);
                integer /= 10u;
            } while (integer != 0);
            while (count > 0) {
                whole_buffer[whole_length++] = reversed[--count];
            }
        } else {
            const uint64_t quotient = scaled / 10000ull;
            uint64_t rest = scaled - quotient * 10000ull;
            for (int i = 3; i >= 0; --i) {
                fraction[i] = static_cast<char>('0' + rest % 10);
                rest /= 10;
            }
            uint64_t integer = quotient;
            char reversed[24];
            size_t count = 0;
            do {
                reversed[count++] = static_cast<char>('0' + integer % 10);
                integer /= 10;
            } while (integer != 0);
            while (count > 0) {
                whole_buffer[whole_length++] = reversed[--count];
            }
        }
    } else {
        // Magnitudes that cannot be scaled exactly fall back to the decimal
        // expansion of the double.
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), "%.25f", magnitude);
        const char* first = buffer;
        const char* dot = first;
        while (*dot && *dot != '.') {
            ++dot;
        }
        whole_length = static_cast<size_t>(dot - first);
        if (whole_length > sizeof(whole_buffer))
            whole_length = sizeof(whole_buffer);
        std::memcpy(whole_buffer, first, whole_length);
        const char* frac = dot < first + 511 && *dot == '.' ? dot + 1 : dot;
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            value = value * 10 + static_cast<unsigned>(frac[i] - '0');
        }
        if (frac[4] >= '5') {
            ++value;
            if (value == 10000) {
                value = 0;
                for (size_t i = whole_length; i-- > 0;) {
                    if (whole_buffer[i] == '9') {
                        whole_buffer[i] = '0';
                    } else {
                        whole_buffer[i] = static_cast<char>(whole_buffer[i] + 1);
                        break;
                    }
                }
            }
        }
        for (int i = 3; i >= 0; --i) {
            fraction[i] = static_cast<char>('0' + value % 10);
            value /= 10;
        }
    }
    size_t fraction_length = 4;
    while (fraction_length > 0 && fraction[fraction_length - 1] == '0') {
        --fraction_length;
    }
    const std::string_view whole(whole_buffer, whole_length);
    size_t whole_start = 0;
    while (whole_start + 1 < whole.size() && whole[whole_start] == '0') {
        ++whole_start;
    }
    const size_t significant = (whole.size() - whole_start) + (fraction_length > 0 ? fraction_length : 0);
    if (significant <= 15) {
        const bool zero = whole.size() - whole_start == 1 && whole[whole_start] == '0';
        if (negative && !(zero && fraction_length == 0)) {
            out.push_back('-');
        }
        out.append(whole, whole_start, whole.size() - whole_start);
        if (fraction_length > 0) {
            out.push_back('.');
            out.append(fraction, fraction_length);
        }
        out += "em";
        return;
    }
    // Fifteen significant digits or more: reproduce JavaScript's round trip
    // through the decimal string.
    std::string text;
    text.reserve(whole.size() + 8);
    if (negative) {
        text.push_back('-');
    }
    text.append(whole, whole_start, whole.size() - whole_start);
    text.push_back('.');
    text.append(fraction, 4);
    out += JsNumberToString(std::strtod(text.c_str(), nullptr));
    out += "em";
}

// KaTeX's `makeEm`: a length in `em` with four decimals.
//
// The magnitudes that cannot be scaled exactly and the numbers with fifteen
// significant digits or more are rare, so they live in a function of their
// own.  What is left is small enough to be inlined into the caller, which
// matters because the markup writer calls it for every length of a formula.
template <class Buffer>
inline void AppendEm(Buffer& out, double n, char suffix = '\0') {
    if (std::isnan(n)) {
        out += "NaNem";
        return;
    }
    if (std::isinf(n)) {
        out += JsNumberToString(n);
        out += "em";
        return;
    }
    const bool negative = n < 0.0;
    const double magnitude = std::fabs(n);
    if (magnitude >= 1e21) {
        // JavaScript's `toFixed` falls back to the exponential form here.
        out += JsNumberToString(n);
        out += "em";
        return;
    }
    bool exact = false;
    const uint64_t scaled = ScaleByTenThousand(magnitude, exact);
    if (!exact) {
        AppendEmRare(out, n);
        if (suffix != '\0') {
            out.push_back(suffix);
        }
        return;
    }
    // The integer part is written into a small buffer; going through
    // std::to_string would allocate a string for every length.
    char whole_buffer[24];
    size_t whole_length = 0;
    char fraction[4] = {'0', '0', '0', '0'};
    if (scaled <= 0xFFFFFFFFull) {
        // The scaled value fits into 32 bits.  The division by a constant
        // becomes a multiplication with a high part, and WebAssembly has
        // no instruction for the 64 bit variant, so the narrow form is
        // much cheaper there.
        const uint32_t quotient = static_cast<uint32_t>(scaled) / 10000u;
        uint32_t rest = static_cast<uint32_t>(scaled) - quotient * 10000u;
        for (int i = 3; i >= 0; --i) {
            fraction[i] = static_cast<char>('0' + rest % 10u);
            rest /= 10u;
        }
        uint32_t integer = quotient;
        char reversed[24];
        size_t count = 0;
        do {
            reversed[count++] = static_cast<char>('0' + integer % 10u);
            integer /= 10u;
        } while (integer != 0);
        while (count > 0) {
            whole_buffer[whole_length++] = reversed[--count];
        }
    } else {
        const uint64_t quotient = scaled / 10000ull;
        uint64_t rest = scaled - quotient * 10000ull;
        for (int i = 3; i >= 0; --i) {
            fraction[i] = static_cast<char>('0' + rest % 10);
            rest /= 10;
        }
        uint64_t integer = quotient;
        char reversed[24];
        size_t count = 0;
        do {
            reversed[count++] = static_cast<char>('0' + integer % 10);
            integer /= 10;
        } while (integer != 0);
        while (count > 0) {
            whole_buffer[whole_length++] = reversed[--count];
        }
    }
    size_t fraction_length = 4;
    while (fraction_length > 0 && fraction[fraction_length - 1] == '0') {
        --fraction_length;
    }
    const std::string_view whole(whole_buffer, whole_length);
    size_t whole_start = 0;
    while (whole_start + 1 < whole.size() && whole[whole_start] == '0') {
        ++whole_start;
    }
    const size_t significant = (whole.size() - whole_start) + (fraction_length > 0 ? fraction_length : 0);
    if (significant <= 15) {
        const bool zero = whole.size() - whole_start == 1 && whole[whole_start] == '0';
        // The text of the length is at most a sign, fifteen digits, a dot and
        // "em", so it is assembled in a buffer of the stack and written into
        // the output in one piece.  Writing it in pieces costs one call into
        // the buffer per piece, and the markup of a formula has a few hundred
        // of those calls.
        char text[32];
        size_t count = 0;
        if (negative && !(zero && fraction_length == 0)) {
            text[count++] = '-';
        }
        const size_t whole_length = whole.size() - whole_start;
        // The copies use wide moves instead of `memcpy`: their length is
        // only known at run time, and a call into the C library costs more
        // in the WebAssembly build than the copy itself.  The whole part is
        // at most fifteen digits and the fraction at most four, so the short
        // copy covers both.
        //
        // The bound of fifteen digits is a consequence of the check above, but
        // the compiler cannot see it, so the length is clamped here.  The
        // clamp never changes a value; it only tells the compiler how long
        // the copy may be.
        const size_t whole_digits = whole_length <= 15 ? whole_length : 15;
        detail::CopyShort(text + count, whole_buffer + whole_start, whole_digits);
        count += whole_digits;
        if (fraction_length > 0) {
            text[count++] = '.';
            detail::CopyShort(text + count, fraction, fraction_length);
            count += fraction_length;
        }
        text[count++] = 'e';
        text[count++] = 'm';
        if (suffix != '\0') {
            text[count++] = suffix;
        }
        out.append(text, count);
        return;
    }
    // Fifteen significant digits or more: the rare path reproduces the
    // digits of the scaled value and rounds the number through its decimal
    // string, exactly like the slow path does.
    AppendEmRare(out, n);
    if (suffix != '\0') {
        out.push_back(suffix);
    }
}


// Equivalent of KaTeX's `makeEm`, returning the value as a string.
inline std::string MakeEm(double n) {
    // The result is at most a sign, a few digits and "em", so it fits into
    // the small buffer of the string.  Reserving would take it out of that
    // buffer and ask the heap for the same few bytes.
    std::string out;
    AppendEm(out, n);
    return out;
}

// Appends KaTeX's `makeEm` result to an existing buffer.
//
// The suffix is written with the number, so that the whole length is one
// piece: the style of a node writes its lengths this way, and every piece of
// the markup costs one call into the buffer.
template <typename Buffer>
inline void MakeEmInto(Buffer& out, double n, char suffix = '\0') {
    AppendEm(out, n, suffix);
}

}  // namespace velocitex::html
