#pragma once

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <system_error>

namespace protoST {

// The double nearest to a decimal numeral (round to nearest, ties to even),
// read without the C locale: the float literals of the lexer (STATUS D51) and
// String >> asNumber. `text` is an optional sign, digits with an optional
// fraction part and an optional exponent (`-12.5e-3`); the callers have
// checked that syntax, and the function refuses anything else (hex, "inf",
// "nan"). A value below half the smallest subnormal is 0.0 or -0.0 and one
// beyond the largest double is +-infinity; the caller decides whether that is
// an error.
//
// std::from_chars is locale-independent (strtod reads LC_NUMERIC, which a
// host embedding libprotost may set to a locale whose decimal separator is a
// comma) and correctly rounded. A standard library without floating-point
// from_chars falls back to strtod.
inline bool parseDecimalDouble(const std::string& text, double& out) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    for (const char* p = begin; p != end; ++p) {
        const char c = *p;
        if (!((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-'))
            return false;
    }
    bool negative = false;
    if (begin != end && (*begin == '+' || *begin == '-')) {
        negative = *begin == '-';
        ++begin;
    }
    if (begin == end || *begin == '+' || *begin == '-') return false;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    double v = 0.0;
    const auto res = std::from_chars(begin, end, v, std::chars_format::general);
    if (res.ptr != end || res.ec == std::errc::invalid_argument) return false;
    if (res.ec == std::errc::result_out_of_range) {
        // from_chars leaves v unspecified here: decide underflow or overflow
        // from the decimal exponent of the first significant digit.
        long long intDigits = 0, leadingFractionZeros = 0;
        bool seenPoint = false, seenSignificant = false;
        const char* p = begin;
        for (; p != end && *p != 'e' && *p != 'E'; ++p) {
            if (*p == '.') { seenPoint = true; continue; }
            if (!seenSignificant && *p == '0') {
                if (seenPoint) ++leadingFractionZeros;
                continue;
            }
            seenSignificant = true;
            if (!seenPoint) ++intDigits;
        }
        long long exponent = 0;
        if (p != end) {
            ++p;
            bool expNegative = false;
            if (p != end && (*p == '+' || *p == '-')) { expNegative = *p == '-'; ++p; }
            for (; p != end; ++p)
                if (exponent < 1000000000LL) exponent = exponent * 10 + (*p - '0');
            if (expNegative) exponent = -exponent;
        }
        // The value is d.ddd x 10^adjusted.
        const long long adjusted =
            (intDigits > 0 ? intDigits - 1 : -(leadingFractionZeros + 1)) + exponent;
        v = adjusted > 0 ? HUGE_VAL : 0.0;
    }
    out = negative ? -v : v;
    return true;
#else
    char* stop = nullptr;
    const double v = std::strtod(begin, &stop);
    if (stop != end) return false;
    out = negative ? -v : v;
    return true;
#endif
}

} // namespace protoST
