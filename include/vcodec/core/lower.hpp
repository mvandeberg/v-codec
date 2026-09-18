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

// Does the double survive a round trip through float?
constexpr bool fits_single(double d) noexcept {
    if (d != d) return true;
    float f = static_cast<float>(d);
    return static_cast<double>(f) == d;
}

} // namespace vcodec::core

#endif // VCODEC_CORE_LOWER_HPP
