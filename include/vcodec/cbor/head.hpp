// CBOR item heads (RFC 8949 §3): major type, additional information, argument. Encoding is
// always shortest-form; decoding reports the form so determinism can be validated.
#ifndef VCODEC_CBOR_HEAD_HPP
#define VCODEC_CBOR_HEAD_HPP

#include <vcodec/core/compiler.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace vcodec::cbor {

enum class major : std::uint8_t {
    uint = 0, nint = 1, bytes = 2, text = 3, array = 4, map = 5, tag = 6, simple = 7,
};

inline constexpr std::uint8_t ai_indefinite = 31;
inline constexpr std::uint8_t break_byte = 0xFF;

struct encoded_head {
    std::array<std::byte, 9> bytes{};
    std::uint8_t size = 0;
    constexpr std::span<const std::byte> view() const noexcept { return { bytes.data(), size }; }
};

// Shortest-form head for a major type and argument.
constexpr encoded_head make_head(major m, std::uint64_t v) noexcept {
    encoded_head h;
    auto mt = static_cast<std::uint8_t>(static_cast<std::uint8_t>(m) << 5);
    auto be = [&](std::uint64_t x, int n) {
        for (int s = (n - 1) * 8; s >= 0; s -= 8) h.bytes[h.size++] = std::byte((x >> s) & 0xFF);
    };
    if (v < 24)                  { h.bytes[h.size++] = std::byte(mt | v); }
    else if (v <= 0xFF)          { h.bytes[h.size++] = std::byte(mt | 24); be(v, 1); }
    else if (v <= 0xFFFF)        { h.bytes[h.size++] = std::byte(mt | 25); be(v, 2); }
    else if (v <= 0xFFFFFFFFull) { h.bytes[h.size++] = std::byte(mt | 26); be(v, 4); }
    else                         { h.bytes[h.size++] = std::byte(mt | 27); be(v, 8); }
    return h;
}

constexpr encoded_head make_indefinite_head(major m) noexcept {
    encoded_head h;
    h.bytes[h.size++] = std::byte((static_cast<std::uint8_t>(m) << 5) | ai_indefinite);
    return h;
}

inline void append_head(std::vector<std::byte>& out, major m, std::uint64_t v) {
    auto h = make_head(m, v);
    out.insert(out.end(), h.bytes.begin(), h.bytes.begin() + h.size);
}

// Encoding of a signed integer as major type 0 or 1.
inline void append_int(std::vector<std::byte>& out, std::int64_t i) {
    if (i >= 0) append_head(out, major::uint, static_cast<std::uint64_t>(i));
    else        append_head(out, major::nint, static_cast<std::uint64_t>(-1 - i));
}

// Compile-time key bytes (integer or text) for prepared_key<>.
template<std::size_t N>
struct key_bytes {
    std::array<std::byte, N> data{};
    std::size_t size = 0;
};

consteval std::vector<std::byte> encode_int_key(std::int64_t k) {
    encoded_head h = k >= 0 ? make_head(major::uint, static_cast<std::uint64_t>(k))
                            : make_head(major::nint, static_cast<std::uint64_t>(-1 - k));
    return std::vector<std::byte>(h.bytes.begin(), h.bytes.begin() + h.size);
}
consteval std::vector<std::byte> encode_text_key(std::string_view name) {
    encoded_head h = make_head(major::text, name.size());
    std::vector<std::byte> v(h.bytes.begin(), h.bytes.begin() + h.size);
    for (char c : name) v.push_back(std::byte(static_cast<unsigned char>(c)));
    return v;
}

// ---- decoding ---------------------------------------------------------------------------

struct parsed_head {
    major         mt{};
    std::uint8_t  ai = 0;          // additional information (0..31)
    std::uint64_t value = 0;       // argument (for ai < 28); for simple: the simple value or float bits
    bool          indefinite = false;
    std::uint8_t  length = 0;      // bytes consumed by the head, 0 on error
    bool          reserved = false;    // ai 28..30
    bool          is_break = false;
    // Deterministic-form check: the smallest ai that could have encoded `value`.
    constexpr bool shortest() const noexcept {
        if (indefinite || is_break || reserved) return false;
        if (mt == major::simple && ai >= 25) return true;   // float widths are checked by value elsewhere
        if (ai < 24) return true;
        if (ai == 24) return value >= 24;
        if (ai == 25) return value > 0xFF;
        if (ai == 26) return value > 0xFFFF;
        return value > 0xFFFFFFFFull;
    }
};

// Parses the head at input[pos]. length == 0 means truncated. reserved/is_break/indefinite
// are reported for the caller to judge.
constexpr parsed_head parse_head(std::span<const std::byte> in, std::size_t pos) noexcept {
    parsed_head h;
    if (pos >= in.size()) return h;
    auto ib = static_cast<std::uint8_t>(in[pos]);
    h.mt = static_cast<major>(ib >> 5);
    h.ai = ib & 31;
    if (ib == break_byte) { h.is_break = true; h.length = 1; return h; }
    if (h.ai < 24) { h.value = h.ai; h.length = 1; return h; }
    if (h.ai == 31) {
        h.indefinite = true; h.length = 1;
        if (h.mt == major::uint || h.mt == major::nint || h.mt == major::tag) h.reserved = true;   // ill-formed
        return h;
    }
    if (h.ai >= 28) { h.reserved = true; h.length = 1; return h; }
    int n = h.ai == 24 ? 1 : h.ai == 25 ? 2 : h.ai == 26 ? 4 : 8;
    if (pos + 1 + static_cast<std::size_t>(n) > in.size()) return h;   // truncated
    std::uint64_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | static_cast<std::uint8_t>(in[pos + 1 + static_cast<std::size_t>(i)]);
    h.value = v;
    h.length = static_cast<std::uint8_t>(1 + n);
    return h;
}

constexpr std::string_view major_name(major m) noexcept {
    switch (m) {
    case major::uint: return "unsigned integer";
    case major::nint: return "negative integer";
    case major::bytes: return "byte string";
    case major::text: return "text string";
    case major::array: return "array";
    case major::map: return "map";
    case major::tag: return "tag";
    case major::simple: return "simple/float";
    }
    return "?";
}

// IANA names for the tags v0.2 knows about, for diagnostics.
constexpr std::string_view tag_name(std::uint64_t t) noexcept {
    switch (t) {
    case 0: return "RFC 3339 string";
    case 1: return "epoch datetime";
    case 2: return "positive bignum";
    case 3: return "negative bignum";
    case 4: return "decimal fraction";
    case 5: return "bigfloat";
    case 21: return "expected base64url";
    case 22: return "expected base64";
    case 23: return "expected base16";
    case 24: return "encoded CBOR";
    case 32: return "URI";
    case 33: return "base64url";
    case 34: return "base64";
    case 36: return "MIME message";
    case 55799: return "self-described CBOR";
    default:
        if (t >= 64 && t <= 87) return "typed array";
        return "";
    }
}

} // namespace vcodec::cbor

#endif // VCODEC_CBOR_HEAD_HPP
