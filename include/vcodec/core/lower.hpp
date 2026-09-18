// Shared lowerings (spec §3): base64, base64url and hex for byte strings in text formats.
#ifndef VCODEC_CORE_LOWER_HPP
#define VCODEC_CORE_LOWER_HPP

#include <vcodec/core/compiler.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vcodec::core {

namespace detail {
inline constexpr char b64_std[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline constexpr char b64_url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
inline constexpr char hex_digits[] = "0123456789abcdef";

constexpr int b64_value(char c, bool url) noexcept {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (url) { if (c == '-') return 62; if (c == '_') return 63; }
    else     { if (c == '+') return 62; if (c == '/') return 63; }
    return -1;
}
constexpr int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
} // namespace detail

// Appends the encoding of `in` to `out`. Standard base64 is padded; base64url is unpadded
// (RFC 4648 §5, as used by JOSE).
inline void base64_encode(std::span<const std::byte> in, std::string& out, bool url = false) {
    const char* tbl = url ? detail::b64_url : detail::b64_std;
    std::size_t i = 0;
    out.reserve(out.size() + (in.size() + 2) / 3 * 4);
    for (; i + 3 <= in.size(); i += 3) {
        std::uint32_t v = (std::uint32_t(in[i]) << 16) | (std::uint32_t(in[i + 1]) << 8) | std::uint32_t(in[i + 2]);
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63]; out += tbl[(v >> 6) & 63]; out += tbl[v & 63];
    }
    if (i + 1 == in.size()) {
        std::uint32_t v = std::uint32_t(in[i]) << 16;
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63];
        if (!url) out += "==";
    } else if (i + 2 == in.size()) {
        std::uint32_t v = (std::uint32_t(in[i]) << 16) | (std::uint32_t(in[i + 1]) << 8);
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63]; out += tbl[(v >> 6) & 63];
        if (!url) out += '=';
    }
}

// Decodes into `out` (appending). Accepts padded or unpadded input for both alphabets.
// Returns false on any invalid character or impossible length.
inline bool base64_decode(std::string_view in, std::vector<std::byte>& out, bool url = false) {
    while (!in.empty() && in.back() == '=') in.remove_suffix(1);
    if (in.size() % 4 == 1) return false;
    out.reserve(out.size() + in.size() * 3 / 4);
    std::uint32_t acc = 0; int bits = 0;
    for (char c : in) {
        int v = detail::b64_value(c, url);
        if (v < 0) return false;
        acc = (acc << 6) | std::uint32_t(v); bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(std::byte((acc >> bits) & 0xFF)); }
    }
    // Trailing bits must be zero for canonical input; tolerate non-canonical padding bits.
    return true;
}

inline void hex_encode(std::span<const std::byte> in, std::string& out) {
    out.reserve(out.size() + in.size() * 2);
    for (std::byte b : in) {
        out += detail::hex_digits[std::uint8_t(b) >> 4];
        out += detail::hex_digits[std::uint8_t(b) & 15];
    }
}

inline bool hex_decode(std::string_view in, std::vector<std::byte>& out) {
    if (in.size() % 2) return false;
    out.reserve(out.size() + in.size() / 2);
    for (std::size_t i = 0; i < in.size(); i += 2) {
        int hi = detail::hex_value(in[i]), lo = detail::hex_value(in[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(std::byte((hi << 4) | lo));
    }
    return true;
}

// ---- UTF-8 ------------------------------------------------------------------------------------

inline constexpr std::size_t utf8_npos = static_cast<std::size_t>(-1);

// Returns utf8_npos if `s` is well-formed UTF-8 (RFC 3629: no overlongs, no surrogates,
// nothing above U+10FFFF), otherwise the byte offset of the first invalid sequence.
constexpr std::size_t validate_utf8(std::string_view s) noexcept {
    std::size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { ++i; continue; }
        auto cont = [&](std::size_t k) { return i + k < n && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80; };
        if (c >= 0xC2 && c <= 0xDF) {
            if (!cont(1)) return i;
            i += 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (!cont(1) || !cont(2)) return i;
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            if (c == 0xE0 && c1 < 0xA0) return i;          // overlong
            if (c == 0xED && c1 >= 0xA0) return i;         // surrogate
            i += 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (!cont(1) || !cont(2) || !cont(3)) return i;
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            if (c == 0xF0 && c1 < 0x90) return i;          // overlong
            if (c == 0xF4 && c1 >= 0x90) return i;         // > U+10FFFF
            i += 4;
        } else {
            return i;
        }
    }
    return utf8_npos;
}

// ---- IEEE 754 binary16 ---------------------------------------------------------------------
// Exact conversions with round-to-nearest-even. Used by CBOR preferred float serialization.

constexpr double from_half(std::uint16_t h) noexcept {
    unsigned sign = (h >> 15) & 1, exp = (h >> 10) & 0x1F, mant = h & 0x3FF;
    double v;
    if (exp == 0)       v = std::ldexp(static_cast<double>(mant), -24);           // subnormal or zero
    else if (exp == 31) v = mant ? std::numeric_limits<double>::quiet_NaN() : std::numeric_limits<double>::infinity();
    else                v = std::ldexp(static_cast<double>(mant + 1024), static_cast<int>(exp) - 25);
    return sign ? -v : v;
}

// Returns nullopt when the value cannot be represented exactly as a half (so callers can fall
// back to single/double). Infinities and NaN are representable; NaN maps to the canonical
// quiet NaN 0x7E00.
constexpr std::optional<std::uint16_t> to_half(double d) noexcept {
    if (d != d) return std::uint16_t(0x7E00);
    std::uint16_t sign = std::signbit(d) ? 0x8000 : 0;
    if (std::isinf(d)) return std::uint16_t(sign | 0x7C00);
    double a = std::fabs(d);
    if (a == 0.0) return sign;
    if (a > 65504.0) return std::nullopt;
    int exp = 0;
    double frac = std::frexp(a, &exp);       // a = frac * 2^exp, frac in [0.5, 1)
    // normal half: a = (1 + m/1024) * 2^(e-15), e in 1..30  →  exp-1 in -14..15
    int e = exp - 1 + 15;
    if (e >= 1) {
        double scaled = frac * 2048.0;       // 1024 <= scaled < 2048
        if (scaled != static_cast<double>(static_cast<std::uint32_t>(scaled))) return std::nullopt;
        auto m = static_cast<std::uint32_t>(scaled) - 1024;
        return static_cast<std::uint16_t>(sign | (static_cast<unsigned>(e) << 10) | m);
    }
    // subnormal half: a = m * 2^-24, m in 1..1023
    double m = std::ldexp(a, 24);
    if (m != static_cast<double>(static_cast<std::uint32_t>(m)) || m >= 1024.0) return std::nullopt;
    return static_cast<std::uint16_t>(sign | static_cast<std::uint32_t>(m));
}

// Nearest half (round to nearest even), for a pinned half width. Overflows to infinity.
constexpr std::uint16_t to_half_rounded(double d) noexcept {
    if (auto h = to_half(d)) return *h;
    std::uint16_t sign = std::signbit(d) ? 0x8000 : 0;
    double a = std::fabs(d);
    if (a >= 65520.0) return static_cast<std::uint16_t>(sign | 0x7C00);
    int exp = 0;
    double frac = std::frexp(a, &exp);
    int e = exp - 1 + 15;
    double scaled; std::uint32_t mant; unsigned ee;
    if (e >= 1) { scaled = frac * 2048.0; ee = static_cast<unsigned>(e); }
    else        { scaled = std::ldexp(a, 24) + 1024.0; ee = 0; }   // subnormal: bias so the rounding below is uniform
    double fl = std::floor(scaled);
    double diff = scaled - fl;
    mant = static_cast<std::uint32_t>(fl);
    if (diff > 0.5 || (diff == 0.5 && (mant & 1))) ++mant;
    if (ee == 0) { mant -= 1024; if (mant >= 1024) { mant -= 1024; ee = 1; } }
    else if (mant >= 2048) { mant -= 1024; ++ee; if (ee >= 31) return static_cast<std::uint16_t>(sign | 0x7C00); }
    mant &= 0x3FF;
    if (ee == 0) return static_cast<std::uint16_t>(sign | mant);
    return static_cast<std::uint16_t>(sign | (ee << 10) | mant);
}

// Does the double survive a round trip through float?
constexpr bool fits_single(double d) noexcept {
    if (d != d) return true;
    float f = static_cast<float>(d);
    return static_cast<double>(f) == d;
}

} // namespace vcodec::core

#endif // VCODEC_CORE_LOWER_HPP
