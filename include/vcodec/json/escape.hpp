// String escaping and UTF-8 validation (spec §3, M4).
#ifndef VCODEC_JSON_ESCAPE_HPP
#define VCODEC_JSON_ESCAPE_HPP

#include <vcodec/core/compiler.hpp>
#include <vcodec/core/fixed_string.hpp>
#include <vcodec/error.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace vcodec::json {

inline constexpr std::size_t npos = static_cast<std::size_t>(-1);

// Returns npos if `s` is well-formed UTF-8 (RFC 3629: no overlongs, no surrogates, nothing
// above U+10FFFF), otherwise the byte offset of the first invalid sequence.
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
    return npos;
}

namespace detail {
inline constexpr char hex_upper[] = "0123456789ABCDEF";

// Bytes that must be escaped: control characters, '"' and '\\'.
constexpr bool needs_escape(unsigned char c) noexcept { return c < 0x20 || c == '"' || c == '\\'; }

template<class Out>
constexpr void put_escape(Out& out, unsigned char c) {
    switch (c) {
    case '"':  out.append("\\\""); break;
    case '\\': out.append("\\\\"); break;
    case '\b': out.append("\\b"); break;
    case '\f': out.append("\\f"); break;
    case '\n': out.append("\\n"); break;
    case '\r': out.append("\\r"); break;
    case '\t': out.append("\\t"); break;
    default: {
        char buf[6] = { '\\', 'u', '0', '0', hex_upper[c >> 4], hex_upper[c & 15] };
        out.append(buf, 6);
    }
    }
}
} // namespace detail

// Appends `s` to `out` with JSON escaping and surrounding quotes. Runs of clean bytes are
// appended in one go. Throws encode_error(invalid_utf8) when validating and `s` is malformed.
template<bool Validate>
inline void write_string(std::string& out, std::string_view s) {
    if constexpr (Validate) {
        if (std::size_t bad = validate_utf8(s); bad != npos) {
            std::string msg = "string contains invalid UTF-8 at byte "; msg += std::to_string(bad);
            throw encode_error(errc::invalid_utf8, std::move(msg));
        }
    }
    out.push_back('"');
    std::size_t run = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (VCODEC_UNLIKELY(detail::needs_escape(c))) {
            if (i > run) out.append(s.data() + run, i - run);
            detail::put_escape(out, c);
            run = i + 1;
        }
    }
    if (s.size() > run) out.append(s.data() + run, s.size() - run);
    out.push_back('"');
}

// Compile-time form for precomputed keys: `"name":` (plus a space when pretty).
template<std::size_t N = 128>
consteval core::fixed_string<N> prepared_key_blob(std::string_view name, bool pretty) {
    core::fixed_string<N> b;
    b.append('"');
    for (char ch : name) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (detail::needs_escape(c)) {
            struct sink { core::fixed_string<N>& b; constexpr void append(const char* s) { b.append(std::string_view(s)); } constexpr void append(const char* s, std::size_t n) { b.append(std::string_view(s, n)); } } sk{b};
            detail::put_escape(sk, c);
        } else b.append(ch);
    }
    b.append("\":");
    if (pretty) b.append(' ');
    return b;
}

} // namespace vcodec::json

#endif // VCODEC_JSON_ESCAPE_HPP
