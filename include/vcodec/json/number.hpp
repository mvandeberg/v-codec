// Number formatting and parsing (spec M4, M6).
#ifndef VCODEC_JSON_NUMBER_HPP
#define VCODEC_JSON_NUMBER_HPP

#include <vcodec/core/compiler.hpp>
#include <vcodec/core/model.hpp>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace vcodec::json {

inline void write_uint(std::string& out, std::uint64_t u) {
    char buf[24];
    auto r = std::to_chars(buf, buf + sizeof buf, u);
    out.append(buf, r.ptr);
}
inline void write_sint(std::string& out, std::int64_t i) {
    char buf[24];
    auto r = std::to_chars(buf, buf + sizeof buf, i);
    out.append(buf, r.ptr);
}
// Shortest round-trip representation. JSON has no NaN or infinity: those become null,
// which is documented in docs/types.md.
inline void write_real(std::string& out, double d) {
    if (!std::isfinite(d)) { out.append("null"); return; }
    char buf[32];
    auto r = std::to_chars(buf, buf + sizeof buf, d);
    out.append(buf, r.ptr);
}

// ---- parsing -----------------------------------------------------------------------------

struct number_scan {
    core::kind    kind = core::kind::undefined;   // uint, sint or real; undefined on error
    std::size_t   length = 0;                     // bytes consumed from the start
    std::uint64_t u = 0;                          // kind::uint
    std::int64_t  i = 0;                          // kind::sint (negative)
    double        d = 0;                          // kind::real
    errc          error = errc::invalid_number;   // valid only when kind == undefined
    bool          overflow = false;               // integer literal too large for 64 bits → treated as real
};

// Scans a JSON number at the start of `s` (RFC 8259 §6 grammar, strict). Integers without
// fraction or exponent become uint/sint with overflow detection; everything else is real.
inline number_scan scan_number(std::string_view s) noexcept {
    number_scan r;
    std::size_t i = 0, n = s.size();
    bool neg = false;
    if (i < n && s[i] == '-') { neg = true; ++i; }
    if (i >= n) { r.error = n == 0 || s[0] == '-' ? errc::invalid_number : errc::invalid_number; return r; }
    if (s[i] == '0') {
        ++i;
        if (i < n && s[i] >= '0' && s[i] <= '9') return r;   // leading zeros are not JSON
    } else if (s[i] >= '1' && s[i] <= '9') {
        while (i < n && s[i] >= '0' && s[i] <= '9') ++i;
    } else {
        return r;
    }
    bool is_real = false;
    if (i < n && s[i] == '.') {
        ++i;
        if (i >= n || !(s[i] >= '0' && s[i] <= '9')) return r;
        while (i < n && s[i] >= '0' && s[i] <= '9') ++i;
        is_real = true;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
        if (i >= n || !(s[i] >= '0' && s[i] <= '9')) return r;
        while (i < n && s[i] >= '0' && s[i] <= '9') ++i;
        is_real = true;
    }
    r.length = i;
    if (!is_real) {
        std::string_view digits = s.substr(neg ? 1 : 0, i - (neg ? 1 : 0));
        std::uint64_t v = 0;
        bool overflow = false;
        for (char c : digits) {
            unsigned d = unsigned(c - '0');
            if (v > (UINT64_MAX - d) / 10) { overflow = true; break; }
            v = v * 10 + d;
        }
        if (!overflow) {
            if (!neg) { r.kind = core::kind::uint; r.u = v; return r; }
            if (v <= std::uint64_t(INT64_MAX) + 1) { r.kind = core::kind::sint; r.i = v == std::uint64_t(INT64_MAX) + 1 ? INT64_MIN : -std::int64_t(v); return r; }
        }
        r.overflow = true;
    }
    auto res = std::from_chars(s.data(), s.data() + i, r.d);
    if (res.ec == std::errc::result_out_of_range) {
        // from_chars leaves r.d unmodified on failure, so decide from the literal itself:
        // an approximate decimal exponent above zero means overflow (not representable);
        // otherwise the value underflowed and is zero.
        std::size_t k = neg ? 1 : 0;
        long exp10 = 0;
        std::size_t int_digits = 0;
        bool leading = true;
        while (k < i && s[k] >= '0' && s[k] <= '9') { if (!(leading && s[k] == '0')) { leading = false; ++int_digits; } ++k; }
        if (k < i && s[k] == '.') { ++k; while (k < i && s[k] >= '0' && s[k] <= '9') ++k; }
        if (k < i && (s[k] == 'e' || s[k] == 'E')) {
            ++k;
            bool eneg = false;
            if (k < i && (s[k] == '+' || s[k] == '-')) { eneg = s[k] == '-'; ++k; }
            while (k < i && s[k] >= '0' && s[k] <= '9') { if (exp10 < 100000) exp10 = exp10 * 10 + (s[k] - '0'); ++k; }
            if (eneg) exp10 = -exp10;
        }
        long magnitude = static_cast<long>(int_digits) - 1 + exp10;
        if (magnitude > 0) { r.error = errc::out_of_range; r.kind = core::kind::undefined; return r; }
        r.d = neg ? -0.0 : 0.0;
    } else if (res.ec != std::errc{}) {
        r.kind = core::kind::undefined; return r;
    }
    r.kind = core::kind::real;
    return r;
}

} // namespace vcodec::json

#endif // VCODEC_JSON_NUMBER_HPP
